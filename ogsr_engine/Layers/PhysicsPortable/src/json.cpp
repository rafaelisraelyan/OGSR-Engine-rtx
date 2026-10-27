#include "physics_portable/json.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace physics_portable
{
namespace
{
struct JsonValue
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;
};

class ParseError : public std::runtime_error
{
public:
    explicit ParseError(const std::string& message) : std::runtime_error(message) {}
};

std::string location(const std::string& text, std::size_t position)
{
    std::size_t line = 1;
    std::size_t column = 1;
    const std::size_t limit = std::min(position, text.size());
    for (std::size_t index = 0; index < limit; ++index)
    {
        if (text[index] == '\n')
        {
            ++line;
            column = 1;
        }
        else
        {
            ++column;
        }
    }
    std::ostringstream stream;
    stream << "line " << line << ", column " << column;
    return stream.str();
}

class JsonParser
{
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool parse(JsonValue& value, std::string* error)
    {
        skipWhitespace();
        if (!parseValue(value))
        {
            *error = error_;
            return false;
        }
        skipWhitespace();
        if (position_ != text_.size())
        {
            *error = makeError("unexpected trailing data");
            return false;
        }
        return true;
    }

private:
    const std::string& text_;
    std::size_t position_ = 0;
    std::string error_;
    int depth_ = 0;

    struct DepthGuard
    {
        explicit DepthGuard(int& depth) : depth(depth) { ++depth; }
        ~DepthGuard() { --depth; }
        int& depth;
    };

    std::string makeError(const std::string& message) const
    {
        return message + " at " + location(text_, position_);
    }

    bool fail(const std::string& message)
    {
        error_ = makeError(message);
        return false;
    }

    void skipWhitespace()
    {
        while (position_ < text_.size())
        {
            const char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++position_;
            else
                break;
        }
    }

    bool consume(char expected)
    {
        skipWhitespace();
        if (position_ >= text_.size() || text_[position_] != expected)
            return false;
        ++position_;
        return true;
    }

    bool parseValue(JsonValue& value)
    {
        skipWhitespace();
        if (position_ >= text_.size())
            return fail("unexpected end of input");

        switch (text_[position_])
        {
        case '{': return parseObject(value);
        case '[': return parseArray(value);
        case '"':
            value.type = JsonValue::Type::String;
            return parseString(value.string);
        case 't':
            if (!consumeLiteral("true"))
                return false;
            value.type = JsonValue::Type::Bool;
            value.boolean = true;
            return true;
        case 'f':
            if (!consumeLiteral("false"))
                return false;
            value.type = JsonValue::Type::Bool;
            value.boolean = false;
            return true;
        case 'n':
            if (!consumeLiteral("null"))
                return false;
            value.type = JsonValue::Type::Null;
            return true;
        default: break;
        }

        if (text_[position_] == '-' || (text_[position_] >= '0' && text_[position_] <= '9'))
            return parseNumber(value);
        return fail("expected a JSON value");
    }

    bool consumeLiteral(const char* literal)
    {
        const std::size_t length = std::strlen(literal);
        if (text_.compare(position_, length, literal) != 0)
            return fail(std::string("expected '") + literal + "'");
        position_ += length;
        return true;
    }

    bool parseNumber(JsonValue& value)
    {
        const char* start = text_.c_str() + position_;
        const char* end = text_.c_str() + text_.size();
        double number = 0.0;
        const auto result = std::from_chars(start, end, number, std::chars_format::general);
        if (result.ec != std::errc() || result.ptr == start)
            return fail("invalid number");
        if (!std::isfinite(number))
            return fail("number is not finite");
        position_ += static_cast<std::size_t>(result.ptr - start);
        value.type = JsonValue::Type::Number;
        value.number = number;
        return true;
    }

    bool parseString(std::string& out)
    {
        if (!consume('"'))
            return fail("expected string");
        out.clear();
        while (position_ < text_.size())
        {
            const char c = text_[position_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return fail("unescaped control character in string");
            if (c != '\\')
            {
                out.push_back(c);
                continue;
            }
            if (position_ >= text_.size())
                return fail("unterminated escape sequence");
            const char escape = text_[position_++];
            switch (escape)
            {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
            {
                const std::uint32_t codePoint = parseUnicodeEscape();
                if (codePoint == 0)
                    return false;
                appendUtf8(out, codePoint);
                break;
            }
            default: return fail("invalid escape sequence");
            }
        }
        return fail("unterminated string");
    }

    std::uint32_t parseHex4()
    {
        if (position_ + 4 > text_.size())
        {
            fail("incomplete unicode escape");
            return 0;
        }
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index)
        {
            const char c = text_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else
            {
                fail("invalid unicode escape");
                return 0;
            }
        }
        return value;
    }

    std::uint32_t parseUnicodeEscape()
    {
        const std::uint32_t first = parseHex4();
        if (first == 0)
            return 0;
        if (first < 0xD800 || first > 0xDFFF)
            return first;
        if (first > 0xDBFF)
        {
            fail("unexpected low surrogate");
            return 0;
        }
        if (position_ + 2 > text_.size() || text_[position_] != '\\' || text_[position_ + 1] != 'u')
        {
            fail("missing low surrogate");
            return 0;
        }
        position_ += 2;
        const std::uint32_t second = parseHex4();
        if (second < 0xDC00 || second > 0xDFFF)
        {
            fail("invalid low surrogate");
            return 0;
        }
        return 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
    }

    static void appendUtf8(std::string& out, std::uint32_t codePoint)
    {
        if (codePoint <= 0x7F)
            out.push_back(static_cast<char>(codePoint));
        else if (codePoint <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else if (codePoint <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool parseObject(JsonValue& value)
    {
        DepthGuard guard(depth_);
        if (depth_ > 128)
            return fail("JSON nesting is too deep");
        if (!consume('{'))
            return false;
        value.type = JsonValue::Type::Object;
        skipWhitespace();
        if (consume('}'))
            return true;
        while (true)
        {
            std::string key;
            if (!parseString(key))
                return false;
            if (!consume(':'))
                return fail("expected ':' after object key");
            JsonValue member;
            if (!parseValue(member))
                return false;
            if (value.object.find(key) != value.object.end())
                return fail("duplicate object key '" + key + "'");
            value.object.emplace(std::move(key), std::move(member));
            skipWhitespace();
            if (consume('}'))
                return true;
            if (!consume(','))
                return fail("expected ',' or '}' in object");
            skipWhitespace();
        }
    }

    bool parseArray(JsonValue& value)
    {
        DepthGuard guard(depth_);
        if (depth_ > 128)
            return fail("JSON nesting is too deep");
        if (!consume('['))
            return false;
        value.type = JsonValue::Type::Array;
        skipWhitespace();
        if (consume(']'))
            return true;
        while (true)
        {
            JsonValue member;
            if (!parseValue(member))
                return false;
            value.array.push_back(std::move(member));
            skipWhitespace();
            if (consume(']'))
                return true;
            if (!consume(','))
                return fail("expected ',' or ']' in array");
            skipWhitespace();
        }
    }
};

void require(bool condition, const std::string& path, const std::string& message)
{
    if (!condition)
        throw ParseError(path + ": " + message);
}

const JsonValue& member(const JsonValue& object, const std::string& key, const std::string& path)
{
    require(object.type == JsonValue::Type::Object, path, "expected object");
    const auto found = object.object.find(key);
    require(found != object.object.end(), path, "missing required field '" + key + "'");
    return found->second;
}

const JsonValue* optionalMember(const JsonValue& object, const std::string& key)
{
    if (object.type != JsonValue::Type::Object)
        return nullptr;
    const auto found = object.object.find(key);
    return found == object.object.end() ? nullptr : &found->second;
}

double asNumber(const JsonValue& value, const std::string& path)
{
    require(value.type == JsonValue::Type::Number, path, "expected number");
    require(std::isfinite(value.number), path, "number must be finite");
    return value.number;
}

std::int64_t asSignedInteger(const JsonValue& value, const std::string& path, std::int64_t minimum, std::int64_t maximum)
{
    const double number = asNumber(value, path);
    require(std::floor(number) == number, path, "expected an integer");
    require(number >= static_cast<double>(minimum) && number <= static_cast<double>(maximum), path,
            "integer is out of range");
    return static_cast<std::int64_t>(number);
}

std::uint32_t asUInt32(const JsonValue& value, const std::string& path)
{
    return static_cast<std::uint32_t>(asSignedInteger(value, path, 0, 0xFFFFFFFFll));
}

std::uint16_t asUInt16(const JsonValue& value, const std::string& path)
{
    return static_cast<std::uint16_t>(asSignedInteger(value, path, 0, 0xFFFF));
}

std::uint8_t asUInt8(const JsonValue& value, const std::string& path)
{
    return static_cast<std::uint8_t>(asSignedInteger(value, path, 0, 0xFF));
}

std::string asString(const JsonValue& value, const std::string& path)
{
    require(value.type == JsonValue::Type::String, path, "expected string");
    return value.string;
}

bool asBool(const JsonValue& value, const std::string& path)
{
    require(value.type == JsonValue::Type::Bool, path, "expected boolean");
    return value.boolean;
}

const std::vector<JsonValue>& asArray(const JsonValue& value, const std::string& path)
{
    require(value.type == JsonValue::Type::Array, path, "expected array");
    return value.array;
}

Transform parseTransform(const JsonValue& value, const std::string& path)
{
    const JsonValue& rotation = member(value, "rotation", path);
    const JsonValue& translation = member(value, "translation", path);
    const auto& r = asArray(rotation, path + ".rotation");
    const auto& t = asArray(translation, path + ".translation");
    require(r.size() == 4, path + ".rotation", "expected [x,y,z,w]");
    require(t.size() == 3, path + ".translation", "expected [x,y,z]");
    Transform result;
    result.rotation = Quat{
        static_cast<float>(asNumber(r[0], path + ".rotation[0]")),
        static_cast<float>(asNumber(r[1], path + ".rotation[1]")),
        static_cast<float>(asNumber(r[2], path + ".rotation[2]")),
        static_cast<float>(asNumber(r[3], path + ".rotation[3]"))};
    result.translation = Vec3{
        static_cast<float>(asNumber(t[0], path + ".translation[0]")),
        static_cast<float>(asNumber(t[1], path + ".translation[1]")),
        static_cast<float>(asNumber(t[2], path + ".translation[2]"))};
    return result;
}

Vec3 parseVec3(const JsonValue& value, const std::string& path)
{
    const auto& array = asArray(value, path);
    require(array.size() == 3, path, "expected [x,y,z]");
    return Vec3{
        static_cast<float>(asNumber(array[0], path + "[0]")),
        static_cast<float>(asNumber(array[1], path + "[1]")),
        static_cast<float>(asNumber(array[2], path + "[2]"))};
}

template <typename Enum>
Enum parseEnum(const JsonValue& value, const std::string& path, const std::map<std::string, Enum>& values)
{
    const std::string text = asString(value, path);
    const auto found = values.find(text);
    require(found != values.end(), path, "unknown enum value '" + text + "'");
    return found->second;
}

BoneDisposition parseDisposition(const JsonValue& value, const std::string& path)
{
    static const std::map<std::string, BoneDisposition> values = {
        {"Body", BoneDisposition::Body},
        {"MergedIntoParent", BoneDisposition::MergedIntoParent},
        {"SkippedInvisible", BoneDisposition::SkippedInvisible},
        {"FreeBody", BoneDisposition::FreeBody},
    };
    return parseEnum(value, path, values);
}

BodyRole parseRole(const JsonValue& value, const std::string& path)
{
    static const std::map<std::string, BodyRole> values = {
        {"Dynamic", BodyRole::Dynamic},
        {"StaticRoot", BodyRole::StaticRoot},
        {"FreeBody", BodyRole::FreeBody},
    };
    return parseEnum(value, path, values);
}

ShapeType parseShapeType(const JsonValue& value, const std::string& path)
{
    static const std::map<std::string, ShapeType> values = {
        {"Box", ShapeType::Box},
        {"Sphere", ShapeType::Sphere},
        {"Cylinder", ShapeType::Cylinder},
    };
    return parseEnum(value, path, values);
}

JointKind parseJointKind(const JsonValue& value, const std::string& path)
{
    static const std::map<std::string, JointKind> values = {
        {"Ball", JointKind::Ball},
        {"Hinge", JointKind::Hinge},
        {"Hinge2", JointKind::Hinge2},
        {"Slider", JointKind::Slider},
        {"Euler3", JointKind::Euler3},
        {"Fixed", JointKind::Fixed},
    };
    return parseEnum(value, path, values);
}

AxisSpace parseAxisSpace(const JsonValue& value, const std::string& path)
{
    static const std::map<std::string, AxisSpace> values = {
        {"Parent", AxisSpace::Parent},
        {"Child", AxisSpace::Child},
        {"World", AxisSpace::World},
    };
    return parseEnum(value, path, values);
}

SwingLimitMode parseSwingMode(const JsonValue& value, const std::string& path)
{
    static const std::map<std::string, SwingLimitMode> values = {
        {"Auto", SwingLimitMode::Auto},
        {"Cone", SwingLimitMode::Cone},
        {"Pyramid", SwingLimitMode::Pyramid},
    };
    return parseEnum(value, path, values);
}

RagdollDescription parseDescription(const JsonValue& value)
{
    RagdollDescription description;
    description.schemaVersion = asUInt32(member(value, "schemaVersion", "description"), "description.schemaVersion");
    const auto name = optionalMember(value, "skeletonName");
    if (name)
        description.skeletonName = asString(*name, "description.skeletonName");
    const auto hash = optionalMember(value, "sourceHash");
    if (hash)
        description.sourceHash = asUInt32(*hash, "description.sourceHash");
    description.rootBody = asUInt16(member(value, "rootBody", "description"), "description.rootBody");

    const auto& bones = asArray(member(value, "bones", "description"), "description.bones");
    for (std::size_t index = 0; index < bones.size(); ++index)
    {
        const std::string path = "description.bones[" + std::to_string(index) + "]";
        const JsonValue& source = bones[index];
        RagdollBoneDesc bone;
        bone.id = asUInt16(member(source, "id", path), path + ".id");
        const std::int64_t parent = asSignedInteger(member(source, "parent", path), path + ".parent", -1, 0xFFFF);
        bone.parent = parent < 0 ? kInvalidBone : static_cast<BoneId>(parent);
        const auto boneName = optionalMember(source, "name");
        if (boneName)
            bone.name = asString(*boneName, path + ".name");
        bone.bindLocal = parseTransform(member(source, "bindLocal", path), path + ".bindLocal");
        bone.visible = asBool(member(source, "visible", path), path + ".visible");
        bone.disposition = parseDisposition(member(source, "disposition", path), path + ".disposition");
        const auto body = optionalMember(source, "body");
        if (body)
        {
            const std::int64_t bodyId = asSignedInteger(*body, path + ".body", -1, 0xFFFF);
            bone.body = bodyId < 0 ? kInvalidBody : static_cast<BodyId>(bodyId);
        }
        const auto flags = optionalMember(source, "sourceShapeFlags");
        if (flags)
            bone.sourceShapeFlags = asUInt32(*flags, path + ".sourceShapeFlags");
        description.bones.push_back(std::move(bone));
    }

    const auto& bodies = asArray(member(value, "bodies", "description"), "description.bodies");
    for (std::size_t index = 0; index < bodies.size(); ++index)
    {
        const std::string path = "description.bodies[" + std::to_string(index) + "]";
        const JsonValue& source = bodies[index];
        RagdollBodyDesc body;
        body.id = asUInt16(member(source, "id", path), path + ".id");
        body.boneId = asUInt16(member(source, "boneId", path), path + ".boneId");
        const auto parent = optionalMember(source, "parentBody");
        if (parent)
        {
            const std::int64_t parentId = asSignedInteger(*parent, path + ".parentBody", -1, 0xFFFF);
            body.parentBody = parentId < 0 ? kInvalidBody : static_cast<BodyId>(parentId);
        }
        body.role = parseRole(member(source, "role", path), path + ".role");
        const auto fixed = optionalMember(source, "isFixed");
        if (fixed)
            body.isFixed = asBool(*fixed, path + ".isFixed");
        body.mass = static_cast<float>(asNumber(member(source, "mass", path), path + ".mass"));
        body.centerOfMass = parseVec3(member(source, "centerOfMass", path), path + ".centerOfMass");
        const auto& inertia = asArray(member(source, "inertia", path), path + ".inertia");
        require(inertia.size() == 6, path + ".inertia", "expected six values [xx,xy,xz,yy,yz,zz]");
        for (std::size_t component = 0; component < 6; ++component)
            body.inertia[component] = static_cast<float>(asNumber(inertia[component], path + ".inertia"));
        body.bindPose = parseTransform(member(source, "bindPose", path), path + ".bindPose");
        const auto merged = optionalMember(source, "mergedBones");
        if (merged)
        {
            for (const JsonValue& bone : asArray(*merged, path + ".mergedBones"))
                body.mergedBones.push_back(asUInt16(bone, path + ".mergedBones[]"));
        }
        const auto group = optionalMember(source, "collisionGroup");
        if (group)
            body.collisionGroup = asUInt32(*group, path + ".collisionGroup");

        const auto& shapes = asArray(member(source, "shapes", path), path + ".shapes");
        for (std::size_t shapeIndex = 0; shapeIndex < shapes.size(); ++shapeIndex)
        {
            const std::string shapePath = path + ".shapes[" + std::to_string(shapeIndex) + "]";
            const JsonValue& shapeSource = shapes[shapeIndex];
            RagdollShapeDesc shape;
            shape.type = parseShapeType(member(shapeSource, "type", shapePath), shapePath + ".type");
            shape.local = parseTransform(member(shapeSource, "local", shapePath), shapePath + ".local");
            const auto half = optionalMember(shapeSource, "halfExtents");
            if (half)
                shape.halfExtents = parseVec3(*half, shapePath + ".halfExtents");
            const auto radius = optionalMember(shapeSource, "radius");
            if (radius)
                shape.radius = static_cast<float>(asNumber(*radius, shapePath + ".radius"));
            const auto height = optionalMember(shapeSource, "height");
            if (height)
                shape.height = static_cast<float>(asNumber(*height, shapePath + ".height"));
            const auto margin = optionalMember(shapeSource, "margin");
            if (margin)
                shape.margin = static_cast<float>(asNumber(*margin, shapePath + ".margin"));
            const auto material = optionalMember(shapeSource, "materialId");
            if (material)
                shape.materialId = asUInt16(*material, shapePath + ".materialId");
            const auto sourceBone = optionalMember(shapeSource, "sourceBoneId");
            if (sourceBone)
                shape.sourceBoneId = asUInt16(*sourceBone, shapePath + ".sourceBoneId");
            const auto flags = optionalMember(shapeSource, "flags");
            if (flags)
                shape.flags = asUInt32(*flags, shapePath + ".flags");
            body.shapes.push_back(std::move(shape));
        }
        description.bodies.push_back(std::move(body));
    }

    const auto& joints = asArray(member(value, "joints", "description"), "description.joints");
    for (std::size_t index = 0; index < joints.size(); ++index)
    {
        const std::string path = "description.joints[" + std::to_string(index) + "]";
        const JsonValue& source = joints[index];
        RagdollJointDesc joint;
        joint.id = asUInt16(member(source, "id", path), path + ".id");
        const auto bone = optionalMember(source, "boneId");
        if (bone)
            joint.boneId = asUInt16(*bone, path + ".boneId");
        joint.parentBody = asUInt16(member(source, "parentBody", path), path + ".parentBody");
        joint.childBody = asUInt16(member(source, "childBody", path), path + ".childBody");
        joint.kind = parseJointKind(member(source, "kind", path), path + ".kind");
        joint.parentFrame = parseTransform(member(source, "parentFrame", path), path + ".parentFrame");
        joint.childFrame = parseTransform(member(source, "childFrame", path), path + ".childFrame");
        const auto axes = optionalMember(source, "axes");
        if (axes)
        {
            const auto& axisArray = asArray(*axes, path + ".axes");
            require(axisArray.size() <= 3, path + ".axes", "at most three axes are supported");
            for (std::size_t axisIndex = 0; axisIndex < axisArray.size(); ++axisIndex)
            {
                const std::string axisPath = path + ".axes[" + std::to_string(axisIndex) + "]";
                const JsonValue& axisSource = axisArray[axisIndex];
                RagdollAxisLimit axis;
                axis.space = parseAxisSpace(member(axisSource, "space", axisPath), axisPath + ".space");
                axis.axis = parseVec3(member(axisSource, "axis", axisPath), axisPath + ".axis");
                axis.lower = static_cast<float>(asNumber(member(axisSource, "lower", axisPath), axisPath + ".lower"));
                axis.upper = static_cast<float>(asNumber(member(axisSource, "upper", axisPath), axisPath + ".upper"));
                const auto sourceLower = optionalMember(axisSource, "sourceLower");
                if (sourceLower)
                    axis.sourceLower = static_cast<float>(asNumber(*sourceLower, axisPath + ".sourceLower"));
                else
                    axis.sourceLower = axis.lower;
                const auto sourceUpper = optionalMember(axisSource, "sourceUpper");
                if (sourceUpper)
                    axis.sourceUpper = static_cast<float>(asNumber(*sourceUpper, axisPath + ".sourceUpper"));
                else
                    axis.sourceUpper = axis.upper;
                const auto spring = optionalMember(axisSource, "spring");
                if (spring)
                    axis.spring = static_cast<float>(asNumber(*spring, axisPath + ".spring"));
                const auto damping = optionalMember(axisSource, "damping");
                if (damping)
                    axis.damping = static_cast<float>(asNumber(*damping, axisPath + ".damping"));
                const auto zero = optionalMember(axisSource, "zero");
                if (zero)
                    axis.zero = static_cast<float>(asNumber(*zero, axisPath + ".zero"));
                joint.axes[axisIndex] = axis;
            }
            joint.axisCount = static_cast<std::uint8_t>(axisArray.size());
        }
        const auto axisCount = optionalMember(source, "axisCount");
        if (axisCount)
        {
            const auto declared = asUInt8(*axisCount, path + ".axisCount");
            require(declared == joint.axisCount, path + ".axisCount", "does not match axes array");
        }
        const auto erp = optionalMember(source, "erp");
        if (erp)
            joint.erp = static_cast<float>(asNumber(*erp, path + ".erp"));
        const auto cfm = optionalMember(source, "cfm");
        if (cfm)
            joint.cfm = static_cast<float>(asNumber(*cfm, path + ".cfm"));
        const auto friction = optionalMember(source, "friction");
        if (friction)
            joint.friction = static_cast<float>(asNumber(*friction, path + ".friction"));
        const auto maxForce = optionalMember(source, "maxForce");
        if (maxForce)
            joint.maxForce = static_cast<float>(asNumber(*maxForce, path + ".maxForce"));
        const auto targetVelocity = optionalMember(source, "targetVelocity");
        if (targetVelocity)
            joint.targetVelocity = static_cast<float>(asNumber(*targetVelocity, path + ".targetVelocity"));
        const auto breakable = optionalMember(source, "breakable");
        if (breakable)
            joint.breakable = asBool(*breakable, path + ".breakable");
        const auto breakForce = optionalMember(source, "breakForce");
        if (breakForce)
            joint.breakForce = static_cast<float>(asNumber(*breakForce, path + ".breakForce"));
        const auto breakTorque = optionalMember(source, "breakTorque");
        if (breakTorque)
            joint.breakTorque = static_cast<float>(asNumber(*breakTorque, path + ".breakTorque"));
        const auto swing = optionalMember(source, "swingLimitMode");
        if (swing)
            joint.swingLimitMode = parseSwingMode(*swing, path + ".swingLimitMode");
        description.joints.push_back(std::move(joint));
    }

    const auto materials = optionalMember(value, "materials");
    if (materials)
    {
        const auto& materialArray = asArray(*materials, "description.materials");
        for (std::size_t index = 0; index < materialArray.size(); ++index)
        {
            const std::string path = "description.materials[" + std::to_string(index) + "]";
            const JsonValue& source = materialArray[index];
            RagdollMaterialDesc material;
            material.id = asUInt16(member(source, "id", path), path + ".id");
            const auto materialName = optionalMember(source, "name");
            if (materialName)
                material.name = asString(*materialName, path + ".name");
            const auto friction = optionalMember(source, "friction");
            if (friction)
                material.friction = static_cast<float>(asNumber(*friction, path + ".friction"));
            const auto restitution = optionalMember(source, "restitution");
            if (restitution)
                material.restitution = static_cast<float>(asNumber(*restitution, path + ".restitution"));
            const auto spring = optionalMember(source, "spring");
            if (spring)
                material.spring = static_cast<float>(asNumber(*spring, path + ".spring"));
            const auto damping = optionalMember(source, "damping");
            if (damping)
                material.damping = static_cast<float>(asNumber(*damping, path + ".damping"));
            description.materials.push_back(std::move(material));
        }
    }
    return description;
}

RagdollInitialState parseInitial(const JsonValue& value, std::size_t bodyCount)
{
    RagdollInitialState initial;
    initial.entityWorld = parseTransform(member(value, "entityWorld", "initial"), "initial.entityWorld");
    initial.rootToEntity = parseTransform(member(value, "rootToEntity", "initial"), "initial.rootToEntity");
    const auto& bodyWorld = asArray(member(value, "bodyWorld", "initial"), "initial.bodyWorld");
    require(bodyWorld.size() == bodyCount, "initial.bodyWorld", "count does not match description.bodies");
    for (std::size_t index = 0; index < bodyWorld.size(); ++index)
        initial.bodyWorld.push_back(parseTransform(bodyWorld[index], "initial.bodyWorld[" + std::to_string(index) + "]"));
    const auto& linear = asArray(member(value, "linearVelocity", "initial"), "initial.linearVelocity");
    const auto& angular = asArray(member(value, "angularVelocity", "initial"), "initial.angularVelocity");
    require(linear.size() == bodyCount, "initial.linearVelocity", "count does not match description.bodies");
    require(angular.size() == bodyCount, "initial.angularVelocity", "count does not match description.bodies");
    for (std::size_t index = 0; index < bodyCount; ++index)
    {
        initial.linearVelocity.push_back(parseVec3(linear[index], "initial.linearVelocity[" + std::to_string(index) + "]"));
        initial.angularVelocity.push_back(parseVec3(angular[index], "initial.angularVelocity[" + std::to_string(index) + "]"));
    }
    const auto enabled = optionalMember(value, "enabled");
    if (enabled)
    {
        const auto& enabledArray = asArray(*enabled, "initial.enabled");
        require(enabledArray.size() == bodyCount, "initial.enabled", "count does not match description.bodies");
        for (const JsonValue& flag : enabledArray)
            initial.enabled.push_back(asBool(flag, "initial.enabled[]") ? 1 : 0);
    }
    else
    {
        initial.enabled.assign(bodyCount, 1);
    }
    return initial;
}

void writeString(std::ostringstream& out, const std::string& value)
{
    out << '"';
    for (char c : value)
    {
        switch (c)
        {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
            else
                out << c;
            break;
        }
    }
    out << '"';
}

void writeVec3(std::ostringstream& out, const Vec3& value)
{
    out << '[' << value.x << ',' << value.y << ',' << value.z << ']';
}

void writeQuat(std::ostringstream& out, const Quat& value)
{
    out << '[' << value.x << ',' << value.y << ',' << value.z << ',' << value.w << ']';
}

void writeTransform(std::ostringstream& out, const Transform& value)
{
    out << "{\"rotation\":";
    writeQuat(out, value.rotation);
    out << ",\"translation\":";
    writeVec3(out, value.translation);
    out << '}';
}

const char* dispositionName(BoneDisposition value)
{
    switch (value)
    {
    case BoneDisposition::Body: return "Body";
    case BoneDisposition::MergedIntoParent: return "MergedIntoParent";
    case BoneDisposition::SkippedInvisible: return "SkippedInvisible";
    case BoneDisposition::FreeBody: return "FreeBody";
    }
    return "Body";
}

const char* roleName(BodyRole value)
{
    switch (value)
    {
    case BodyRole::Dynamic: return "Dynamic";
    case BodyRole::StaticRoot: return "StaticRoot";
    case BodyRole::FreeBody: return "FreeBody";
    }
    return "Dynamic";
}

const char* shapeTypeName(ShapeType value)
{
    switch (value)
    {
    case ShapeType::Box: return "Box";
    case ShapeType::Sphere: return "Sphere";
    case ShapeType::Cylinder: return "Cylinder";
    }
    return "Box";
}

const char* jointKindName(JointKind value)
{
    switch (value)
    {
    case JointKind::Ball: return "Ball";
    case JointKind::Hinge: return "Hinge";
    case JointKind::Hinge2: return "Hinge2";
    case JointKind::Slider: return "Slider";
    case JointKind::Euler3: return "Euler3";
    case JointKind::Fixed: return "Fixed";
    }
    return "Ball";
}

const char* axisSpaceName(AxisSpace value)
{
    switch (value)
    {
    case AxisSpace::Parent: return "Parent";
    case AxisSpace::Child: return "Child";
    case AxisSpace::World: return "World";
    }
    return "Child";
}

const char* swingModeName(SwingLimitMode value)
{
    switch (value)
    {
    case SwingLimitMode::Auto: return "Auto";
    case SwingLimitMode::Cone: return "Cone";
    case SwingLimitMode::Pyramid: return "Pyramid";
    }
    return "Auto";
}

void writeDescription(std::ostringstream& out, const RagdollDescription& description)
{
    out << "{\"schemaVersion\":" << description.schemaVersion;
    out << ",\"skeletonName\":";
    writeString(out, description.skeletonName);
    out << ",\"sourceHash\":" << description.sourceHash;
    out << ",\"rootBody\":" << description.rootBody;
    out << ",\"bones\":[";
    for (std::size_t index = 0; index < description.bones.size(); ++index)
    {
        const RagdollBoneDesc& bone = description.bones[index];
        if (index)
            out << ',';
        out << "{\"id\":" << bone.id << ",\"parent\":";
        if (bone.parent == kInvalidBone)
            out << -1;
        else
            out << bone.parent;
        out << ",\"name\":";
        writeString(out, bone.name);
        out << ",\"bindLocal\":";
        writeTransform(out, bone.bindLocal);
        out << ",\"visible\":" << (bone.visible ? "true" : "false");
        out << ",\"disposition\":";
        writeString(out, dispositionName(bone.disposition));
        out << ",\"body\":";
        if (bone.body == kInvalidBody)
            out << -1;
        else
            out << bone.body;
        out << ",\"sourceShapeFlags\":" << bone.sourceShapeFlags << '}';
    }
    out << "],\"bodies\":[";
    for (std::size_t index = 0; index < description.bodies.size(); ++index)
    {
        const RagdollBodyDesc& body = description.bodies[index];
        if (index)
            out << ',';
        out << "{\"id\":" << body.id << ",\"boneId\":" << body.boneId << ",\"parentBody\":";
        if (body.parentBody == kInvalidBody)
            out << -1;
        else
            out << body.parentBody;
        out << ",\"role\":";
        writeString(out, roleName(body.role));
        out << ",\"isFixed\":" << (body.isFixed ? "true" : "false");
        out << ",\"mass\":" << body.mass;
        out << ",\"centerOfMass\":";
        writeVec3(out, body.centerOfMass);
        out << ",\"inertia\":[";
        for (int component = 0; component < 6; ++component)
        {
            if (component)
                out << ',';
            out << body.inertia[component];
        }
        out << "],\"bindPose\":";
        writeTransform(out, body.bindPose);
        out << ",\"mergedBones\":[";
        for (std::size_t merged = 0; merged < body.mergedBones.size(); ++merged)
        {
            if (merged)
                out << ',';
            out << body.mergedBones[merged];
        }
        out << "],\"collisionGroup\":" << body.collisionGroup << ",\"shapes\":[";
        for (std::size_t shapeIndex = 0; shapeIndex < body.shapes.size(); ++shapeIndex)
        {
            const RagdollShapeDesc& shape = body.shapes[shapeIndex];
            if (shapeIndex)
                out << ',';
            out << "{\"type\":";
            writeString(out, shapeTypeName(shape.type));
            out << ",\"local\":";
            writeTransform(out, shape.local);
            out << ",\"halfExtents\":";
            writeVec3(out, shape.halfExtents);
            out << ",\"radius\":" << shape.radius;
            out << ",\"height\":" << shape.height;
            out << ",\"margin\":" << shape.margin;
            out << ",\"materialId\":" << shape.materialId;
            out << ",\"sourceBoneId\":" << shape.sourceBoneId;
            out << ",\"flags\":" << shape.flags << '}';
        }
        out << "]}";
    }
    out << "],\"joints\":[";
    for (std::size_t index = 0; index < description.joints.size(); ++index)
    {
        const RagdollJointDesc& joint = description.joints[index];
        if (index)
            out << ',';
        out << "{\"id\":" << joint.id << ",\"boneId\":" << joint.boneId;
        out << ",\"parentBody\":" << joint.parentBody << ",\"childBody\":" << joint.childBody;
        out << ",\"kind\":";
        writeString(out, jointKindName(joint.kind));
        out << ",\"parentFrame\":";
        writeTransform(out, joint.parentFrame);
        out << ",\"childFrame\":";
        writeTransform(out, joint.childFrame);
        out << ",\"axisCount\":" << static_cast<int>(joint.axisCount);
        out << ",\"axes\":[";
        for (std::uint8_t axis = 0; axis < joint.axisCount; ++axis)
        {
            const RagdollAxisLimit& limit = joint.axes[axis];
            if (axis)
                out << ',';
            out << "{\"space\":";
            writeString(out, axisSpaceName(limit.space));
            out << ",\"axis\":";
            writeVec3(out, limit.axis);
            out << ",\"lower\":" << limit.lower << ",\"upper\":" << limit.upper;
            out << ",\"sourceLower\":" << limit.sourceLower << ",\"sourceUpper\":" << limit.sourceUpper;
            out << ",\"spring\":" << limit.spring << ",\"damping\":" << limit.damping;
            out << ",\"zero\":" << limit.zero << '}';
        }
        out << "],\"erp\":" << joint.erp << ",\"cfm\":" << joint.cfm;
        out << ",\"friction\":" << joint.friction << ",\"maxForce\":" << joint.maxForce;
        out << ",\"targetVelocity\":" << joint.targetVelocity;
        out << ",\"breakable\":" << (joint.breakable ? "true" : "false");
        out << ",\"breakForce\":" << joint.breakForce << ",\"breakTorque\":" << joint.breakTorque;
        out << ",\"swingLimitMode\":";
        writeString(out, swingModeName(joint.swingLimitMode));
        out << '}';
    }
    out << "],\"materials\":[";
    for (std::size_t index = 0; index < description.materials.size(); ++index)
    {
        const RagdollMaterialDesc& material = description.materials[index];
        if (index)
            out << ',';
        out << "{\"id\":" << material.id << ",\"name\":";
        writeString(out, material.name);
        out << ",\"friction\":" << material.friction << ",\"restitution\":" << material.restitution;
        out << ",\"spring\":" << material.spring << ",\"damping\":" << material.damping << '}';
    }
    out << "]}";
}

void writeInitial(std::ostringstream& out, const RagdollInitialState& initial)
{
    out << "{\"entityWorld\":";
    writeTransform(out, initial.entityWorld);
    out << ",\"rootToEntity\":";
    writeTransform(out, initial.rootToEntity);
    out << ",\"bodyWorld\":[";
    for (std::size_t index = 0; index < initial.bodyWorld.size(); ++index)
    {
        if (index)
            out << ',';
        writeTransform(out, initial.bodyWorld[index]);
    }
    out << "],\"linearVelocity\":[";
    for (std::size_t index = 0; index < initial.linearVelocity.size(); ++index)
    {
        if (index)
            out << ',';
        writeVec3(out, initial.linearVelocity[index]);
    }
    out << "],\"angularVelocity\":[";
    for (std::size_t index = 0; index < initial.angularVelocity.size(); ++index)
    {
        if (index)
            out << ',';
        writeVec3(out, initial.angularVelocity[index]);
    }
    out << "],\"enabled\":[";
    if (initial.enabled.empty())
    {
        for (std::size_t index = 0; index < initial.bodyWorld.size(); ++index)
        {
            if (index)
                out << ',';
            out << "true";
        }
    }
    else
    {
        for (std::size_t index = 0; index < initial.enabled.size(); ++index)
        {
            if (index)
                out << ',';
            out << (initial.enabled[index] ? "true" : "false");
        }
    }
    out << "]}";
}

} // namespace

bool parseRagdollCapture(const std::string& json, RagdollCaptureDocument& document, std::string* error)
{
    try
    {
        JsonValue root;
        std::string syntaxError;
        JsonParser parser(json);
        if (!parser.parse(root, &syntaxError))
        {
            if (error)
                *error = syntaxError;
            return false;
        }
        require(root.type == JsonValue::Type::Object, "root", "expected object");
        const double schema = asNumber(member(root, "schemaVersion", "root"), "root.schemaVersion");
        require(schema == 1.0, "root.schemaVersion", "only schema 1 is supported");
        document.description = parseDescription(member(root, "description", "root"));
        document.initial = parseInitial(member(root, "initial", "root"), document.description.bodies.size());
        const JsonValue* source = optionalMember(root, "source");
        if (source)
        {
            const auto commit = optionalMember(*source, "ogsrCommit");
            if (commit)
                document.ogsrCommit = asString(*commit, "source.ogsrCommit");
            const auto visual = optionalMember(*source, "visual");
            if (visual)
                document.visual = asString(*visual, "source.visual");
            const auto hash = optionalMember(*source, "modelHash");
            if (hash)
                document.modelHash = asUInt32(*hash, "source.modelHash");
            const auto fix = optionalMember(*source, "fixCopJoints");
            if (fix)
                document.fixCopJoints = asBool(*fix, "source.fixCopJoints");
        }
        if (!validateDescription(document.description, error))
            return false;
        if (!validateInitialState(document.description, document.initial, error))
            return false;
        return true;
    }
    catch (const ParseError& exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
    catch (const std::exception& exception)
    {
        if (error)
            *error = exception.what();
        return false;
    }
}

bool writeRagdollCapture(const RagdollCaptureDocument& document, std::string& json, std::string* error)
{
    if (!validateDescription(document.description, error))
        return false;
    if (!validateInitialState(document.description, document.initial, error))
        return false;

    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(9);
    out << "{\"schemaVersion\":1,\"source\":{\"ogsrCommit\":";
    writeString(out, document.ogsrCommit);
    out << ",\"visual\":";
    writeString(out, document.visual);
    out << ",\"modelHash\":" << document.modelHash;
    out << ",\"fixCopJoints\":" << (document.fixCopJoints ? "true" : "false") << "},\"description\":";
    writeDescription(out, document.description);
    out << ",\"initial\":";
    writeInitial(out, document.initial);
    out << '}';
    json = out.str();
    return true;
}

} // namespace physics_portable
