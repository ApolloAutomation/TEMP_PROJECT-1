// Sparkplug B Implementation for ESPHome
// Based on Sparkplug B Specification v3.0
// https://sparkplug.eclipse.org/

#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace sparkplug_b {

// Sparkplug B Metric Data Types
enum class DataType : uint32_t {
    Unknown = 0,
    Int8 = 1,
    Int16 = 2,
    Int32 = 3,
    Int64 = 4,
    UInt8 = 5,
    UInt16 = 6,
    UInt32 = 7,
    UInt64 = 8,
    Float = 9,
    Double = 10,
    Boolean = 11,
    String = 12,
    DateTime = 13,
    Text = 14
};

// Simple Protocol Buffer encoder for Sparkplug B
// Sparkplug B uses protobuf with specific field numbers
class ProtobufEncoder {
public:
    std::vector<uint8_t> buffer;

    void clear() {
        buffer.clear();
    }

    // Encode varint (variable-length integer)
    void encode_varint(uint64_t value) {
        while (value > 0x7F) {
            buffer.push_back((value & 0x7F) | 0x80);
            value >>= 7;
        }
        buffer.push_back(value & 0x7F);
    }

    // Encode field tag (field number + wire type)
    void encode_tag(uint32_t field_number, uint8_t wire_type) {
        encode_varint((field_number << 3) | wire_type);
    }

    // Wire types
    static const uint8_t WIRE_VARINT = 0;
    static const uint8_t WIRE_64BIT = 1;
    static const uint8_t WIRE_LENGTH_DELIMITED = 2;
    static const uint8_t WIRE_32BIT = 5;

    // Encode uint64 field
    void encode_uint64(uint32_t field_number, uint64_t value) {
        encode_tag(field_number, WIRE_VARINT);
        encode_varint(value);
    }

    // Encode uint32 field
    void encode_uint32(uint32_t field_number, uint32_t value) {
        encode_tag(field_number, WIRE_VARINT);
        encode_varint(value);
    }

    // Encode bool field
    void encode_bool(uint32_t field_number, bool value) {
        encode_tag(field_number, WIRE_VARINT);
        encode_varint(value ? 1 : 0);
    }

    // Encode float field (32-bit)
    void encode_float(uint32_t field_number, float value) {
        encode_tag(field_number, WIRE_32BIT);
        uint32_t bits;
        memcpy(&bits, &value, sizeof(bits));
        buffer.push_back(bits & 0xFF);
        buffer.push_back((bits >> 8) & 0xFF);
        buffer.push_back((bits >> 16) & 0xFF);
        buffer.push_back((bits >> 24) & 0xFF);
    }

    // Encode double field (64-bit)
    void encode_double(uint32_t field_number, double value) {
        encode_tag(field_number, WIRE_64BIT);
        uint64_t bits;
        memcpy(&bits, &value, sizeof(bits));
        for (int i = 0; i < 8; i++) {
            buffer.push_back((bits >> (i * 8)) & 0xFF);
        }
    }

    // Encode string field
    void encode_string(uint32_t field_number, const std::string& value) {
        encode_tag(field_number, WIRE_LENGTH_DELIMITED);
        encode_varint(value.length());
        buffer.insert(buffer.end(), value.begin(), value.end());
    }

    // Encode bytes field
    void encode_bytes(uint32_t field_number, const std::vector<uint8_t>& value) {
        encode_tag(field_number, WIRE_LENGTH_DELIMITED);
        encode_varint(value.size());
        buffer.insert(buffer.end(), value.begin(), value.end());
    }

    // Encode nested message
    void encode_nested(uint32_t field_number, const std::vector<uint8_t>& nested) {
        encode_tag(field_number, WIRE_LENGTH_DELIMITED);
        encode_varint(nested.size());
        buffer.insert(buffer.end(), nested.begin(), nested.end());
    }
};

// Sparkplug B Metric structure
struct Metric {
    std::string name;
    uint64_t alias;
    uint64_t timestamp;
    DataType datatype;
    bool is_null;

    // Value union (only one is used based on datatype)
    union {
        int64_t int_value;
        uint64_t uint_value;
        float float_value;
        double double_value;
        bool bool_value;
    };
    std::string string_value;

    Metric() : alias(0), timestamp(0), datatype(DataType::Unknown), is_null(false),
               int_value(0) {}
};

// Sparkplug B Payload encoder
class SparkplugPayload {
public:
    uint64_t timestamp;
    uint64_t seq;
    std::vector<Metric> metrics;

    SparkplugPayload() : timestamp(0), seq(0) {}

    // Sparkplug B Payload field numbers (from sparkplug_b.proto)
    static const uint32_t FIELD_TIMESTAMP = 1;
    static const uint32_t FIELD_METRICS = 2;
    static const uint32_t FIELD_SEQ = 3;

    // Metric field numbers
    static const uint32_t METRIC_NAME = 1;
    static const uint32_t METRIC_ALIAS = 2;
    static const uint32_t METRIC_TIMESTAMP = 3;
    static const uint32_t METRIC_DATATYPE = 4;
    static const uint32_t METRIC_IS_NULL = 6;
    static const uint32_t METRIC_INT_VALUE = 7;
    static const uint32_t METRIC_LONG_VALUE = 8;
    static const uint32_t METRIC_FLOAT_VALUE = 9;
    static const uint32_t METRIC_DOUBLE_VALUE = 10;
    static const uint32_t METRIC_BOOLEAN_VALUE = 11;
    static const uint32_t METRIC_STRING_VALUE = 12;

    std::vector<uint8_t> encode_metric(const Metric& m) {
        ProtobufEncoder enc;

        // Name (required for BIRTH, optional for DATA with alias)
        if (!m.name.empty()) {
            enc.encode_string(METRIC_NAME, m.name);
        }

        // Alias
        if (m.alias > 0) {
            enc.encode_uint64(METRIC_ALIAS, m.alias);
        }

        // Timestamp
        if (m.timestamp > 0) {
            enc.encode_uint64(METRIC_TIMESTAMP, m.timestamp);
        }

        // Datatype
        enc.encode_uint32(METRIC_DATATYPE, static_cast<uint32_t>(m.datatype));

        // Is null
        if (m.is_null) {
            enc.encode_bool(METRIC_IS_NULL, true);
        } else {
            // Value based on datatype
            switch (m.datatype) {
                case DataType::Int8:
                case DataType::Int16:
                case DataType::Int32:
                    enc.encode_uint32(METRIC_INT_VALUE, static_cast<uint32_t>(m.int_value));
                    break;
                case DataType::Int64:
                    enc.encode_uint64(METRIC_LONG_VALUE, static_cast<uint64_t>(m.int_value));
                    break;
                case DataType::UInt8:
                case DataType::UInt16:
                case DataType::UInt32:
                    enc.encode_uint32(METRIC_INT_VALUE, static_cast<uint32_t>(m.uint_value));
                    break;
                case DataType::UInt64:
                case DataType::DateTime:
                    enc.encode_uint64(METRIC_LONG_VALUE, m.uint_value);
                    break;
                case DataType::Float:
                    enc.encode_float(METRIC_FLOAT_VALUE, m.float_value);
                    break;
                case DataType::Double:
                    enc.encode_double(METRIC_DOUBLE_VALUE, m.double_value);
                    break;
                case DataType::Boolean:
                    enc.encode_bool(METRIC_BOOLEAN_VALUE, m.bool_value);
                    break;
                case DataType::String:
                case DataType::Text:
                    enc.encode_string(METRIC_STRING_VALUE, m.string_value);
                    break;
                default:
                    break;
            }
        }

        return enc.buffer;
    }

    std::vector<uint8_t> encode() {
        ProtobufEncoder enc;

        // Timestamp
        enc.encode_uint64(FIELD_TIMESTAMP, timestamp);

        // Metrics
        for (const auto& m : metrics) {
            enc.encode_nested(FIELD_METRICS, encode_metric(m));
        }

        // Sequence number
        enc.encode_uint64(FIELD_SEQ, seq);

        return enc.buffer;
    }

    void add_metric_float(const std::string& name, uint64_t alias, float value, uint64_t ts = 0) {
        Metric m;
        m.name = name;
        m.alias = alias;
        m.timestamp = ts;
        m.datatype = DataType::Float;
        m.float_value = value;
        m.is_null = std::isnan(value);
        metrics.push_back(m);
    }

    void add_metric_int(const std::string& name, uint64_t alias, int64_t value, uint64_t ts = 0) {
        Metric m;
        m.name = name;
        m.alias = alias;
        m.timestamp = ts;
        m.datatype = DataType::Int64;
        m.int_value = value;
        m.is_null = false;
        metrics.push_back(m);
    }

    void add_metric_uint(const std::string& name, uint64_t alias, uint64_t value, uint64_t ts = 0) {
        Metric m;
        m.name = name;
        m.alias = alias;
        m.timestamp = ts;
        m.datatype = DataType::UInt64;
        m.uint_value = value;
        m.is_null = false;
        metrics.push_back(m);
    }

    void add_metric_bool(const std::string& name, uint64_t alias, bool value, uint64_t ts = 0) {
        Metric m;
        m.name = name;
        m.alias = alias;
        m.timestamp = ts;
        m.datatype = DataType::Boolean;
        m.bool_value = value;
        m.is_null = false;
        metrics.push_back(m);
    }

    void add_metric_string(const std::string& name, uint64_t alias, const std::string& value, uint64_t ts = 0) {
        Metric m;
        m.name = name;
        m.alias = alias;
        m.timestamp = ts;
        m.datatype = DataType::String;
        m.string_value = value;
        m.is_null = false;
        metrics.push_back(m);
    }
};

// Metric aliases for TEMP_PRO-1 sensors
// Using aliases allows DATA messages to be smaller (no metric names needed)
// Only 8 metrics: 6 PM counts + 2 status
enum MetricAlias : uint64_t {
    // PM Count ranges (#/m³)
    ALIAS_PM_0_3_TO_0_5 = 1,
    ALIAS_PM_0_5_TO_1 = 2,
    ALIAS_PM_1_TO_2_5 = 3,
    ALIAS_PM_2_5_TO_4 = 4,
    ALIAS_PM_4_TO_10 = 5,
    ALIAS_PM_TOTAL_COUNT = 6,
    // Status
    ALIAS_DEVICE_ONLINE = 7,
    ALIAS_UPTIME = 8
};

// Sparkplug B Configuration
// Change this to match your organization
const char* SPB_GROUP_ID = "ApolloAutomation";

} // namespace sparkplug_b
