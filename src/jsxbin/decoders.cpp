#include "util.h"
#include "decoders.h"
#include "nodes/nodes.h"

#define is_capital_alpha(x) x - 0x41 <= 0x19

using namespace jsxbin::decoders;

// Markers / constants...
enum Markers : char {
    ID_REFERENCE = 0x7A,
    NEGATIVE_NUMBER = 0x79,
    NUMBER_8_BYTES = 0x38,
    NUMBER_4_BYTES = 0x34,
    NUMBER_2_BYTES = 0x32,
    BOOL_TRUE = 0x74,
    BOOL_FALSE = 0x66,
};

// begin utility functions...
string fromISO8859(const unsigned char &value) {
    string result;
    if (value < 0x80) {
        result.push_back((char) value);
    } else {
        result.push_back((char) (0xC0 | value >> 6));
        result.push_back((char) (0x80 | (value & 0x3f)));
    }
    return result;
}
// end utility functions.

enum LiteralType {
    NUMBER,
    UTF8_STRING
};

string d_number_primitive(Reader &reader, int length, bool negative) {
     vector<byte> buffer(length);

    for (int i = 0; i < length; ++i) {
        buffer[i] = d_byte(reader);
    }

    short sign = negative ? -1 : 1;

    // using the length, return the appropriate interpretation of the value...
    switch (length) {
        case 8:
            // result is a double...
            return to_string(*((double *)buffer.data()) * sign);
        case 4:
            // result is an integer...
            return to_string(*((uint32_t *)buffer.data()) * sign);
        case 2:
            // result is a short...
            return to_string(*((uint16_t *)buffer.data()) * sign);
        default:
            return "";
    }
}

string d_literal_primitive(Reader &reader, LiteralType literalType) {
    if (reader.decrement_node_depth()) {
        return "";
    }

    bool negative = false;
    if (reader.peek(0) == NEGATIVE_NUMBER) {
        negative = true;
        reader.step();
    }

    char marker = reader.peek(0);

    if (marker == NUMBER_4_BYTES) {
        reader.step();
        string number = d_number_primitive(reader, 4, negative);
        return number;

    } else if (marker == NUMBER_2_BYTES) {
        reader.step();
        string number = d_number_primitive(reader, 2, negative);
        return number;

    } else {
        byte num = d_byte(reader);

        if (negative) {
            return to_string(-1 * (int) num);
        } else {
            if (literalType == LiteralType::NUMBER) {
                return to_string((unsigned char) num);
            } else {
                return fromISO8859((unsigned char) num);
            }
        }
    }
}

int decoders::d_literal_num(Reader &reader) {
    string value = d_literal_primitive(reader, LiteralType::NUMBER);
    return value.empty() ? 0 : stoi(value);
}


AstNode *decoders::d_node(Reader &reader) {
    char marker = reader.pop();

    AstNode *node = nodes::get((NodeType) marker, reader);

    if (node != nullptr) {
        node->parse();

        return node;
    }

    // TODO: handle this
    return nullptr;
}

string decoders::d_number(Reader &reader) {
    char marker = reader.peek(0);
    string num;

    // if the marker suggests
    if (marker == NUMBER_8_BYTES) {
        reader.step();
        num = d_number_primitive(reader, 8, false);
    } else {
        num = d_literal_primitive(reader, LiteralType::NUMBER);
    }

    return num.empty() ? "0" : num;
}

byte decoders::d_byte(Reader& reader) {
    static const string alphabet_upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ" "abcdef";

    // if it is nested, decrement depth level and return 0
    // (sorta feels like it should never happen)
    if (reader.decrement_node_depth()) {
        return static_cast<byte>(0);
    }

    char cur = reader.pop();

    // if result is capital letter...
    if (is_capital_alpha(cur)) {
        return static_cast<byte>(cur - 'A');
    } else {
        // cur must be within [103, 111]
        int n = (cur - 0x67) * 32; // ranges from 0 to 256
        char second = reader.pop();
        size_t up_index = alphabet_upper.find(second);

        // takes advantage of 8-bit overflow for encoding...
        return (byte) (n + up_index);
    }
}

string decoders::d_variant(Reader &reader) {
    using jsxbin::utils::replace_str_inplace;

    string result;

    // types are 'a' or 'b':null 'c':boolean 'd':number 'e':string
    uint8_t type = reader.pop() - 'a';

    switch (type) {
        case 0: // 'a' - also recognized as a null at runtime.
        case 1: // 'b' - null always encoded to 'b'
            // null type
            result = "null";
            break;
        case 2: // 'c'
            // boolean type
            result = d_bool(reader) ? "true" : "false";
            break;
        case 3: // 'd'
            // number type
            result = d_number(reader);
            break;
        case 4: // 'e'
            // string type
            result = d_string(reader);

            replace_str_inplace(result, "\\", "\\\\");
            replace_str_inplace(result, "\"", "\\\"");
            replace_str_inplace(result, "\n", "\\n");
            replace_str_inplace(result, "\t", "\\t");
            replace_str_inplace(result, "\t", "\\t");
            replace_str_inplace(result, "\r", "\\r");

            result = '\"' + result + '\"';
            break;

        case 13: // 'n' | NO_VARIANT
            result = "";
            break;

        default:
            // TODO: Handle this
            printf("Unexpected: %c\n", type);
            break;
    }

    return result;
}

bool decoders::d_bool(Reader& reader) {
    char marker = reader.pop();

    if (marker == BOOL_TRUE)
        return true;
    else if (marker == BOOL_FALSE)
        return false;

    // TODO: Handle this
    return false;
}

string decoders::d_string(Reader &reader) {

    // Parse length of string...
    string parsed_len = d_literal_primitive(reader, LiteralType::NUMBER);
    int length = parsed_len.empty() ? 0 : stoi(parsed_len);

    if (length == 0)
        return "";

    string buf;
    for (int i = 0; i < length; ++i) {
        buf += d_literal_primitive(reader, LiteralType::UTF8_STRING);
    }

    return buf;
}

reference decoders::d_ref(Reader &reader) {
    string id = d_ident(reader);
    bool flag = false;

    if (reader.get_version() >= JsxbinVersion::v20) {
        flag = d_bool(reader);
    }

    return reference{ id, flag };
}

int decoders::d_length(Reader &reader) {
    string value = d_literal_primitive(reader, LiteralType::NUMBER);
    return value.empty() ? 0 : abs(stoi(value));
}

string decoders::d_ident(Reader &reader) {
    char marker = reader.peek(0);

    if (marker != ID_REFERENCE) {
        string id = to_string(d_length(reader));
        return reader.get_symbol(id);
    } else {
        char type = reader.pop();
        string name = d_string(reader);
        string id = to_string(d_length(reader));
        reader.add_symbol(id, name);
        return name;
    }
}

vector<AstNode *> decoders::d_children(Reader &reader) {
    int length = d_length(reader);
    if (length == 0) {
        return {};
    }

    vector<AstNode *> result;
    for (int i = 0; i < length; ++i) {
        AstNode *child = d_node(reader);
        if (child != nullptr)
            result.push_back(child);
    }

    return result;
}

line_info decoders::d_line_info(Reader &reader) {
    line_info result;

    result.line_number = d_length(reader);
    result.child = d_node(reader);

    int length = d_length(reader);

    for (int i = 0; i < length; ++i) {
        result.labels.push_back(d_ident(reader));
    }

    return result;
}

function_signature decoders::d_fn_sig(Reader &reader) {
    function_signature result;

    int length = d_length(reader);
    if (length > 0) {
        for (int i = 0; i < length; ++i) {
            string parameterName = d_ident(reader);
            int paramLength = d_length(reader);

            // separate local variables from parameter list...
            if (paramLength > 0x1ffffc70 && paramLength < 0x202fbf00)
                result.parameters.insert_or_assign(parameterName, paramLength);
            else
                result.local_vars.insert_or_assign(parameterName, paramLength);
        }
    }

    result.header_1 = d_length(reader);
    result.type = d_length(reader);
    result.header_3 = d_length(reader);
    result.name = d_ident(reader);
    result.header_5 = d_literal_num(reader);
    return result;
}

// decoding utilities...
bool decoders::valid_id(const string& value) {
    static const regex identifier("^[a-zA-Z_$][0-9a-zA-Z_$]*$");
    return regex_match(value.c_str(), identifier);
}

bool decoders::is_integer(const string& value) {
    size_t len = value.length();

    for (int i = 0; i < len; ++i) {
        if (!isdigit(value[i])) {
            return false;
        }
    }

    return true;
}