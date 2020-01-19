//
// Header-only tiny glTF 2.0 loader and serializer.
//
//
// The MIT License (MIT)
//
// Copyright (c) 2015 - 2018 Syoyo Fujita, Aurélien Chatelain and many
// contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Version:
//  - v2.1.0 Add draco compression.
//  - v2.0.1 Add comparsion feature(Thanks to @Selmar).
//  - v2.0.0 glTF 2.0!.
//
// Tiny glTF loader is using following third party libraries:
//
//  - jsonhpp: C++ JSON library.
//  - base64: base64 decode/encode library.
//  - stb_image: Image loading library.
//
#ifndef TINY_GLTF_H_
#define TINY_GLTF_H_

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef __ANDROID__
#ifdef TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#include <android/asset_manager.h>
#endif
#endif

namespace tinygltf {

#define TINYGLTF_MODE_POINTS (0)
#define TINYGLTF_MODE_LINE (1)
#define TINYGLTF_MODE_LINE_LOOP (2)
#define TINYGLTF_MODE_LINE_STRIP (3)
#define TINYGLTF_MODE_TRIANGLES (4)
#define TINYGLTF_MODE_TRIANGLE_STRIP (5)
#define TINYGLTF_MODE_TRIANGLE_FAN (6)

#define TINYGLTF_COMPONENT_TYPE_BYTE (5120)
#define TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE (5121)
#define TINYGLTF_COMPONENT_TYPE_SHORT (5122)
#define TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT (5123)
#define TINYGLTF_COMPONENT_TYPE_INT (5124)
#define TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT (5125)
#define TINYGLTF_COMPONENT_TYPE_FLOAT (5126)
#define TINYGLTF_COMPONENT_TYPE_DOUBLE (5130)

#define TINYGLTF_TEXTURE_FILTER_NEAREST (9728)
#define TINYGLTF_TEXTURE_FILTER_LINEAR (9729)
#define TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST (9984)
#define TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST (9985)
#define TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR (9986)
#define TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR (9987)

#define TINYGLTF_TEXTURE_WRAP_REPEAT (10497)
#define TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE (33071)
#define TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT (33648)

// Redeclarations of the above for technique.parameters.
#define TINYGLTF_PARAMETER_TYPE_BYTE (5120)
#define TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE (5121)
#define TINYGLTF_PARAMETER_TYPE_SHORT (5122)
#define TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT (5123)
#define TINYGLTF_PARAMETER_TYPE_INT (5124)
#define TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT (5125)
#define TINYGLTF_PARAMETER_TYPE_FLOAT (5126)

#define TINYGLTF_PARAMETER_TYPE_FLOAT_VEC2 (35664)
#define TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 (35665)
#define TINYGLTF_PARAMETER_TYPE_FLOAT_VEC4 (35666)

#define TINYGLTF_PARAMETER_TYPE_INT_VEC2 (35667)
#define TINYGLTF_PARAMETER_TYPE_INT_VEC3 (35668)
#define TINYGLTF_PARAMETER_TYPE_INT_VEC4 (35669)

#define TINYGLTF_PARAMETER_TYPE_BOOL (35670)
#define TINYGLTF_PARAMETER_TYPE_BOOL_VEC2 (35671)
#define TINYGLTF_PARAMETER_TYPE_BOOL_VEC3 (35672)
#define TINYGLTF_PARAMETER_TYPE_BOOL_VEC4 (35673)

#define TINYGLTF_PARAMETER_TYPE_FLOAT_MAT2 (35674)
#define TINYGLTF_PARAMETER_TYPE_FLOAT_MAT3 (35675)
#define TINYGLTF_PARAMETER_TYPE_FLOAT_MAT4 (35676)

#define TINYGLTF_PARAMETER_TYPE_SAMPLER_2D (35678)

// End parameter types

#define TINYGLTF_TYPE_VEC2 (2)
#define TINYGLTF_TYPE_VEC3 (3)
#define TINYGLTF_TYPE_VEC4 (4)
#define TINYGLTF_TYPE_MAT2 (32 + 2)
#define TINYGLTF_TYPE_MAT3 (32 + 3)
#define TINYGLTF_TYPE_MAT4 (32 + 4)
#define TINYGLTF_TYPE_SCALAR (64 + 1)
#define TINYGLTF_TYPE_VECTOR (64 + 4)
#define TINYGLTF_TYPE_MATRIX (64 + 16)

#define TINYGLTF_IMAGE_FORMAT_JPEG (0)
#define TINYGLTF_IMAGE_FORMAT_PNG (1)
#define TINYGLTF_IMAGE_FORMAT_BMP (2)
#define TINYGLTF_IMAGE_FORMAT_GIF (3)

#define TINYGLTF_TEXTURE_FORMAT_ALPHA (6406)
#define TINYGLTF_TEXTURE_FORMAT_RGB (6407)
#define TINYGLTF_TEXTURE_FORMAT_RGBA (6408)
#define TINYGLTF_TEXTURE_FORMAT_LUMINANCE (6409)
#define TINYGLTF_TEXTURE_FORMAT_LUMINANCE_ALPHA (6410)

#define TINYGLTF_TEXTURE_TARGET_TEXTURE2D (3553)
#define TINYGLTF_TEXTURE_TYPE_UNSIGNED_BYTE (5121)

#define TINYGLTF_TARGET_ARRAY_BUFFER (34962)
#define TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER (34963)

#define TINYGLTF_SHADER_TYPE_VERTEX_SHADER (35633)
#define TINYGLTF_SHADER_TYPE_FRAGMENT_SHADER (35632)

#define TINYGLTF_DOUBLE_EPS (1.e-12)
#define TINYGLTF_DOUBLE_EQUAL(a, b) (std::fabs((b) - (a)) < TINYGLTF_DOUBLE_EPS)

#ifdef __ANDROID__
#ifdef TINYGLTF_ANDROID_LOAD_FROM_ASSETS
  AAssetManager* asset_manager = nullptr;
#endif
#endif

typedef enum {
  NULL_TYPE = 0,
  NUMBER_TYPE = 1,
  INT_TYPE = 2,
  BOOL_TYPE = 3,
  STRING_TYPE = 4,
  ARRAY_TYPE = 5,
  BINARY_TYPE = 6,
  OBJECT_TYPE = 7
} Type;

static inline int32_t GetComponentSizeInBytes(uint32_t componentType) {
  if (componentType == TINYGLTF_COMPONENT_TYPE_BYTE) {
    return 1;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
    return 1;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_SHORT) {
    return 2;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
    return 2;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_INT) {
    return 4;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
    return 4;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
    return 4;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE) {
    return 8;
  } else {
    // Unknown componenty type
    return -1;
  }
}

static inline int32_t GetTypeSizeInBytes(uint32_t ty) {
  if (ty == TINYGLTF_TYPE_SCALAR) {
    return 1;
  } else if (ty == TINYGLTF_TYPE_VEC2) {
    return 2;
  } else if (ty == TINYGLTF_TYPE_VEC3) {
    return 3;
  } else if (ty == TINYGLTF_TYPE_VEC4) {
    return 4;
  } else if (ty == TINYGLTF_TYPE_MAT2) {
    return 4;
  } else if (ty == TINYGLTF_TYPE_MAT3) {
    return 9;
  } else if (ty == TINYGLTF_TYPE_MAT4) {
    return 16;
  } else {
    // Unknown componenty type
    return -1;
  }
}

bool IsDataURI(const std::string &in);
bool DecodeDataURI(std::vector<unsigned char> *out, std::string &mime_type,
                   const std::string &in, size_t reqBytes, bool checkSize);

#ifdef __clang__
#pragma clang diagnostic push
// Suppress warning for : static Value null_value
// https://stackoverflow.com/questions/15708411/how-to-deal-with-global-constructor-warning-in-clang
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wpadded"
#endif

// Simple class to represent JSON object
class Value {
 public:
  typedef std::vector<Value> Array;
  typedef std::map<std::string, Value> Object;

  Value() : type_(NULL_TYPE) {}

  explicit Value(bool b) : type_(BOOL_TYPE) { boolean_value_ = b; }
  explicit Value(int i) : type_(INT_TYPE) { int_value_ = i; }
  explicit Value(double n) : type_(NUMBER_TYPE) { number_value_ = n; }
  explicit Value(const std::string &s) : type_(STRING_TYPE) {
    string_value_ = s;
  }
  explicit Value(const unsigned char *p, size_t n) : type_(BINARY_TYPE) {
    binary_value_.resize(n);
    memcpy(binary_value_.data(), p, n);
  }
  explicit Value(const Array &a) : type_(ARRAY_TYPE) {
    array_value_ = Array(a);
  }
  explicit Value(const Object &o) : type_(OBJECT_TYPE) {
    object_value_ = Object(o);
  }

  char Type() const { return static_cast<const char>(type_); }

  bool IsBool() const { return (type_ == BOOL_TYPE); }

  bool IsInt() const { return (type_ == INT_TYPE); }

  bool IsNumber() const { return (type_ == NUMBER_TYPE); }

  bool IsString() const { return (type_ == STRING_TYPE); }

  bool IsBinary() const { return (type_ == BINARY_TYPE); }

  bool IsArray() const { return (type_ == ARRAY_TYPE); }

  bool IsObject() const { return (type_ == OBJECT_TYPE); }

  // Accessor
  template <typename T>
  const T &Get() const;
  template <typename T>
  T &Get();

  // Lookup value from an array
  const Value &Get(int idx) const {
    static Value null_value;
    assert(IsArray());
    assert(idx >= 0);
    return (static_cast<size_t>(idx) < array_value_.size())
               ? array_value_[static_cast<size_t>(idx)]
               : null_value;
  }

  // Lookup value from a key-value pair
  const Value &Get(const std::string &key) const {
    static Value null_value;
    assert(IsObject());
    Object::const_iterator it = object_value_.find(key);
    return (it != object_value_.end()) ? it->second : null_value;
  }

  size_t ArrayLen() const {
    if (!IsArray()) return 0;
    return array_value_.size();
  }

  // Valid only for object type.
  bool Has(const std::string &key) const {
    if (!IsObject()) return false;
    Object::const_iterator it = object_value_.find(key);
    return (it != object_value_.end()) ? true : false;
  }

  // List keys
  std::vector<std::string> Keys() const {
    std::vector<std::string> keys;
    if (!IsObject()) return keys;  // empty

    for (Object::const_iterator it = object_value_.begin();
         it != object_value_.end(); ++it) {
      keys.push_back(it->first);
    }

    return keys;
  }

  size_t Size() const { return (IsArray() ? ArrayLen() : Keys().size()); }

  bool operator==(const tinygltf::Value &other) const;

 protected:
  int type_;

  int int_value_;
  double number_value_;
  std::string string_value_;
  std::vector<unsigned char> binary_value_;
  Array array_value_;
  Object object_value_;
  bool boolean_value_;
};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define TINYGLTF_VALUE_GET(ctype, var)            \
  template <>                                     \
  inline const ctype &Value::Get<ctype>() const { \
    return var;                                   \
  }                                               \
  template <>                                     \
  inline ctype &Value::Get<ctype>() {             \
    return var;                                   \
  }
TINYGLTF_VALUE_GET(bool, boolean_value_)
TINYGLTF_VALUE_GET(double, number_value_)
TINYGLTF_VALUE_GET(int, int_value_)
TINYGLTF_VALUE_GET(std::string, string_value_)
TINYGLTF_VALUE_GET(std::vector<unsigned char>, binary_value_)
TINYGLTF_VALUE_GET(Value::Array, array_value_)
TINYGLTF_VALUE_GET(Value::Object, object_value_)
#undef TINYGLTF_VALUE_GET

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++98-compat"
#pragma clang diagnostic ignored "-Wpadded"
#endif

/// Agregate object for representing a color
using ColorValue = std::array<double, 4>;

struct Parameter {
  bool bool_value = false;
  bool has_number_value = false;
  std::string string_value;
  std::vector<double> number_array;
  std::map<std::string, double> json_double_value;
  double number_value = 0.0;
  // context sensitive methods. depending the type of the Parameter you are
  // accessing, these are either valid or not
  // If this parameter represent a texture map in a material, will return the
  // texture index

  /// Return the index of a texture if this Parameter is a texture map.
  /// Returned value is only valid if the parameter represent a texture from a
  /// material
  int TextureIndex() const {
    const auto it = json_double_value.find("index");
    if (it != std::end(json_double_value)) {
      return int(it->second);
    }
    return -1;
  }

  /// Material factor, like the roughness or metalness of a material
  /// Returned value is only valid if the parameter represent a texture from a
  /// material
  double Factor() const { return number_value; }

  /// Return the color of a material
  /// Returned value is only valid if the parameter represent a texture from a
  /// material
  ColorValue ColorFactor() const {
    return {
        {// this agregate intialize the std::array object, and uses C++11 RVO.
         number_array[0], number_array[1], number_array[2],
         (number_array.size() > 3 ? number_array[3] : 1.0)}};
  }

  bool operator==(const Parameter &) const;
};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#endif

typedef std::map<std::string, Parameter> ParameterMap;
typedef std::map<std::string, Value> ExtensionMap;

struct AnimationChannel {
  int sampler;              // required
  int target_node;          // required (index of the node to target)
  std::string target_path;  // required in ["translation", "rotation", "scale",
                            // "weights"]
  Value extras;

  AnimationChannel() : sampler(-1), target_node(-1) {}
  bool operator==(const AnimationChannel &) const;
};

struct AnimationSampler {
  int input;                  // required
  int output;                 // required
  std::string interpolation;  // in ["LINEAR", "STEP", "CATMULLROMSPLINE",
                              // "CUBICSPLINE"], default "LINEAR"
  Value extras;

  AnimationSampler() : input(-1), output(-1), interpolation("LINEAR") {}
  bool operator==(const AnimationSampler &) const;
};

struct Animation {
  std::string name;
  std::vector<AnimationChannel> channels;
  std::vector<AnimationSampler> samplers;
  Value extras;

  bool operator==(const Animation &) const;
};

struct Skin {
  std::string name;
  int inverseBindMatrices;  // required here but not in the spec
  int skeleton;             // The index of the node used as a skeleton root
  std::vector<int> joints;  // Indices of skeleton nodes

  Skin() {
    inverseBindMatrices = -1;
    skeleton = -1;
  }
  bool operator==(const Skin &) const;
};

struct Sampler {
  std::string name;
  int minFilter;  // ["NEAREST", "LINEAR", "NEAREST_MIPMAP_LINEAR",
                  // "LINEAR_MIPMAP_NEAREST", "NEAREST_MIPMAP_LINEAR",
                  // "LINEAR_MIPMAP_LINEAR"]
  int magFilter;  // ["NEAREST", "LINEAR"]
  int wrapS;      // ["CLAMP_TO_EDGE", "MIRRORED_REPEAT", "REPEAT"], default
                  // "REPEAT"
  int wrapT;      // ["CLAMP_TO_EDGE", "MIRRORED_REPEAT", "REPEAT"], default
                  // "REPEAT"
  int wrapR;      // TinyGLTF extension
  Value extras;

  Sampler()
      : minFilter(TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR),
        magFilter(TINYGLTF_TEXTURE_FILTER_LINEAR),
        wrapS(TINYGLTF_TEXTURE_WRAP_REPEAT),
        wrapT(TINYGLTF_TEXTURE_WRAP_REPEAT),
        wrapR(TINYGLTF_TEXTURE_WRAP_REPEAT){}
  bool operator==(const Sampler &) const;
};

struct Image {
  std::string name;
  int width;
  int height;
  int component;
  std::vector<unsigned char> image;
  int bufferView;        // (required if no uri)
  std::string mimeType;  // (required if no uri) ["image/jpeg", "image/png",
                         // "image/bmp", "image/gif"]
  std::string uri;       // (required if no mimeType)
  Value extras;
  ExtensionMap extensions;

  // When this flag is true, data is stored to `image` in as-is format(e.g. jpeg
  // compressed for "image/jpeg" mime) This feature is good if you use custom
  // image loader function. (e.g. delayed decoding of images for faster glTF
  // parsing) Default parser for Image does not provide as-is loading feature at
  // the moment. (You can manipulate this by providing your own LoadImageData
  // function)
  bool as_is;

  Image() : as_is(false) {
    bufferView = -1;
    width = -1;
    height = -1;
    component = -1;
  }
  bool operator==(const Image &) const;
};

struct Texture {
  std::string name;

  int sampler;
  int source;
  Value extras;
  ExtensionMap extensions;

  Texture() : sampler(-1), source(-1) {}
  bool operator==(const Texture &) const;
};

// Each extension should be stored in a ParameterMap.
// members not in the values could be included in the ParameterMap
// to keep a single material model
struct Material {
  std::string name;

  ParameterMap values;            // PBR metal/roughness workflow
  ParameterMap additionalValues;  // normal/occlusion/emissive values

  ExtensionMap extensions;
  Value extras;

  bool operator==(const Material &) const;
};

struct BufferView {
  std::string name;
  int buffer;         // Required
  size_t byteOffset;  // minimum 0, default 0
  size_t byteLength;  // required, minimum 1
  size_t byteStride;  // minimum 4, maximum 252 (multiple of 4), default 0 =
                      // understood to be tightly packed
  int target;         // ["ARRAY_BUFFER", "ELEMENT_ARRAY_BUFFER"]
  Value extras;
  bool dracoDecoded;  // Flag indicating this has been draco decoded

  BufferView() : byteOffset(0), byteStride(0), dracoDecoded(false) {}
  bool operator==(const BufferView &) const;
};

struct Accessor {
  int bufferView;  // optional in spec but required here since sparse accessor
                   // are not supported
  std::string name;
  size_t byteOffset;
  bool normalized;    // optinal.
  int componentType;  // (required) One of TINYGLTF_COMPONENT_TYPE_***
  size_t count;       // required
  int type;           // (required) One of TINYGLTF_TYPE_***   ..
  Value extras;

  std::vector<double> minValues;  // optional
  std::vector<double> maxValues;  // optional

  // TODO(syoyo): "sparse"

  ///
  /// Utility function to compute byteStride for a given bufferView object.
  /// Returns -1 upon invalid glTF value or parameter configuration.
  ///
  int ByteStride(const BufferView &bufferViewObject) const {
    if (bufferViewObject.byteStride == 0) {
      // Assume data is tightly packed.
      int componentSizeInBytes =
          GetComponentSizeInBytes(static_cast<uint32_t>(componentType));
      if (componentSizeInBytes <= 0) {
        return -1;
      }

      int typeSizeInBytes = GetTypeSizeInBytes(static_cast<uint32_t>(type));
      if (typeSizeInBytes <= 0) {
        return -1;
      }

      return componentSizeInBytes * typeSizeInBytes;
    } else {
      // Check if byteStride is a mulple of the size of the accessor's component
      // type.
      int componentSizeInBytes =
          GetComponentSizeInBytes(static_cast<uint32_t>(componentType));
      if (componentSizeInBytes <= 0) {
        return -1;
      }

      if ((bufferViewObject.byteStride % uint32_t(componentSizeInBytes)) != 0) {
        return -1;
      }
      return static_cast<int>(bufferViewObject.byteStride);
    }

    return 0;
  }

  Accessor() { bufferView = -1; }
  bool operator==(const tinygltf::Accessor &) const;
};

struct PerspectiveCamera {
  double aspectRatio;  // min > 0
  double yfov;         // required. min > 0
  double zfar;         // min > 0
  double znear;        // required. min > 0

  PerspectiveCamera()
      : aspectRatio(0.0),
        yfov(0.0),
        zfar(0.0)  // 0 = use infinite projecton matrix
        ,
        znear(0.0) {}
  bool operator==(const PerspectiveCamera &) const;

  ExtensionMap extensions;
  Value extras;
};

struct OrthographicCamera {
  double xmag;   // required. must not be zero.
  double ymag;   // required. must not be zero.
  double zfar;   // required. `zfar` must be greater than `znear`.
  double znear;  // required

  OrthographicCamera() : xmag(0.0), ymag(0.0), zfar(0.0), znear(0.0) {}
  bool operator==(const OrthographicCamera &) const;

  ExtensionMap extensions;
  Value extras;
};

struct Camera {
  std::string type;  // required. "perspective" or "orthographic"
  std::string name;

  PerspectiveCamera perspective;
  OrthographicCamera orthographic;

  Camera() {}
  bool operator==(const Camera &) const;

  ExtensionMap extensions;
  Value extras;
};

struct Primitive {
  std::map<std::string, int> attributes;  // (required) A dictionary object of
                                          // integer, where each integer
                                          // is the index of the accessor
                                          // containing an attribute.
  int material;  // The index of the material to apply to this primitive
                 // when rendering.
  int indices;   // The index of the accessor that contains the indices.
  int mode;      // one of TINYGLTF_MODE_***
  std::vector<std::map<std::string, int> > targets;  // array of morph targets,
  // where each target is a dict with attribues in ["POSITION, "NORMAL",
  // "TANGENT"] pointing
  // to their corresponding accessors
  ExtensionMap extensions;
  Value extras;

  Primitive() {
    material = -1;
    indices = -1;
  }
  bool operator==(const Primitive &) const;
};

struct Mesh {
  std::string name;
  std::vector<Primitive> primitives;
  std::vector<double> weights;  // weights to be applied to the Morph Targets
  std::vector<std::map<std::string, int> > targets;
  ExtensionMap extensions;
  Value extras;

  bool operator==(const Mesh &) const;
};

class Node {
 public:
  Node() : camera(-1), skin(-1), mesh(-1) {}

  Node(const Node &rhs) {
    camera = rhs.camera;

    name = rhs.name;
    skin = rhs.skin;
    mesh = rhs.mesh;
    children = rhs.children;
    rotation = rhs.rotation;
    scale = rhs.scale;
    translation = rhs.translation;
    matrix = rhs.matrix;
    weights = rhs.weights;

    extensions = rhs.extensions;
    extras = rhs.extras;
  }
  ~Node() {}
  bool operator==(const Node &) const;

  int camera;  // the index of the camera referenced by this node

  std::string name;
  int skin;
  int mesh;
  std::vector<int> children;
  std::vector<double> rotation;     // length must be 0 or 4
  std::vector<double> scale;        // length must be 0 or 3
  std::vector<double> translation;  // length must be 0 or 3
  std::vector<double> matrix;       // length must be 0 or 16
  std::vector<double> weights;  // The weights of the instantiated Morph Target

  ExtensionMap extensions;
  Value extras;
};

struct Buffer {
  std::string name;
  std::vector<unsigned char> data;
  std::string
      uri;  // considered as required here but not in the spec (need to clarify)
  Value extras;

  bool operator==(const Buffer &) const;
};

struct Asset {
  std::string version;  // required
  std::string generator;
  std::string minVersion;
  std::string copyright;
  ExtensionMap extensions;
  Value extras;

  bool operator==(const Asset &) const;
};

struct Scene {
  std::string name;
  std::vector<int> nodes;

  ExtensionMap extensions;
  Value extras;

  bool operator==(const Scene &) const;
};

struct Light {
  std::string name;
  std::vector<double> color;
  std::string type;

  bool operator==(const Light &) const;
};

class Model {
 public:
  Model() {}
  ~Model() {}
  bool operator==(const Model &) const;

  std::vector<Accessor> accessors;
  std::vector<Animation> animations;
  std::vector<Buffer> buffers;
  std::vector<BufferView> bufferViews;
  std::vector<Material> materials;
  std::vector<Mesh> meshes;
  std::vector<Node> nodes;
  std::vector<Texture> textures;
  std::vector<Image> images;
  std::vector<Skin> skins;
  std::vector<Sampler> samplers;
  std::vector<Camera> cameras;
  std::vector<Scene> scenes;
  std::vector<Light> lights;
  ExtensionMap extensions;

  int defaultScene;
  std::vector<std::string> extensionsUsed;
  std::vector<std::string> extensionsRequired;

  Asset asset;

  Value extras;
};

enum SectionCheck {
  NO_REQUIRE = 0x00,
  REQUIRE_SCENE = 0x01,
  REQUIRE_SCENES = 0x02,
  REQUIRE_NODES = 0x04,
  REQUIRE_ACCESSORS = 0x08,
  REQUIRE_BUFFERS = 0x10,
  REQUIRE_BUFFER_VIEWS = 0x20,
  REQUIRE_ALL = 0x3f
};

///
/// LoadImageDataFunction type. Signature for custom image loading callbacks.
///
typedef bool (*LoadImageDataFunction)(Image *, const int, std::string *, std::string *,
                                      int, int, const unsigned char *, int,
                                      void *);

///
/// WriteImageDataFunction type. Signature for custom image writing callbacks.
///
typedef bool (*WriteImageDataFunction)(const std::string *, const std::string *,
                                       Image *, bool, void *);

#ifndef TINYGLTF_NO_STB_IMAGE
// Declaration of default image loader callback
bool LoadImageData(Image *image, const int image_idx, std::string *err, std::string *warn,
                   int req_width, int req_height, const unsigned char *bytes,
                   int size, void *);
#endif

#ifndef TINYGLTF_NO_STB_IMAGE_WRITE
// Declaration of default image writer callback
bool WriteImageData(const std::string *basepath, const std::string *filename,
                    Image *image, bool embedImages, void *);
#endif

///
/// FilExistsFunction type. Signature for custom filesystem callbacks.
///
typedef bool (*FileExistsFunction)(const std::string &abs_filename, void *);

///
/// ExpandFilePathFunction type. Signature for custom filesystem callbacks.
///
typedef std::string (*ExpandFilePathFunction)(const std::string &, void *);

///
/// ReadWholeFileFunction type. Signature for custom filesystem callbacks.
///
typedef bool (*ReadWholeFileFunction)(std::vector<unsigned char> *,
                                      std::string *, const std::string &,
                                      void *);

///
/// WriteWholeFileFunction type. Signature for custom filesystem callbacks.
///
typedef bool (*WriteWholeFileFunction)(std::string *, const std::string &,
                                       const std::vector<unsigned char> &,
                                       void *);

///
/// A structure containing all required filesystem callbacks and a pointer to
/// their user data.
///
struct FsCallbacks {
  FileExistsFunction FileExists;
  ExpandFilePathFunction ExpandFilePath;
  ReadWholeFileFunction ReadWholeFile;
  WriteWholeFileFunction WriteWholeFile;

  void *user_data;  // An argument that is passed to all fs callbacks
};

#ifndef TINYGLTF_NO_FS
// Declaration of default filesystem callbacks

bool FileExists(const std::string &abs_filename, void *);

std::string ExpandFilePath(const std::string &filepath, void *);

bool ReadWholeFile(std::vector<unsigned char> *out, std::string *err,
                   const std::string &filepath, void *);

bool WriteWholeFile(std::string *err, const std::string &filepath,
                    const std::vector<unsigned char> &contents, void *);
#endif

class TinyGLTF {
 public:
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++98-compat"
#endif

  TinyGLTF() : bin_data_(nullptr), bin_size_(0), is_binary_(false) {}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

  ~TinyGLTF() {}

  ///
  /// Loads glTF ASCII asset from a file.
  /// Set warning message to `warn` for example it fails to load asserts.
  /// Returns false and set error string to `err` if there's an error.
  ///
  bool LoadASCIIFromFile(Model *model, std::string *err, std::string *warn,
                         const std::string &filename,
                         unsigned int check_sections = REQUIRE_ALL);

  ///
  /// Loads glTF ASCII asset from string(memory).
  /// `length` = strlen(str);
  /// Set warning message to `warn` for example it fails to load asserts.
  /// Returns false and set error string to `err` if there's an error.
  ///
  bool LoadASCIIFromString(Model *model, std::string *err, std::string *warn,
                           const char *str, const unsigned int length,
                           const std::string &base_dir,
                           unsigned int check_sections = REQUIRE_ALL);

  ///
  /// Loads glTF binary asset from a file.
  /// Set warning message to `warn` for example it fails to load asserts.
  /// Returns false and set error string to `err` if there's an error.
  ///
  bool LoadBinaryFromFile(Model *model, std::string *err, std::string *warn,
                          const std::string &filename,
                          unsigned int check_sections = REQUIRE_ALL);

  ///
  /// Loads glTF binary asset from memory.
  /// `length` = strlen(str);
  /// Set warning message to `warn` for example it fails to load asserts.
  /// Returns false and set error string to `err` if there's an error.
  ///
  bool LoadBinaryFromMemory(Model *model, std::string *err, std::string *warn,
                            const unsigned char *bytes,
                            const unsigned int length,
                            const std::string &base_dir = "",
                            unsigned int check_sections = REQUIRE_ALL);

  ///
  /// Write glTF to file.
  ///
  bool WriteGltfSceneToFile(Model *model, const std::string &filename,
                            bool embedImages,
                            bool embedBuffers,
                            bool prettyPrint,
                            bool writeBinary);

  ///
  /// Set callback to use for loading image data
  ///
  void SetImageLoader(LoadImageDataFunction LoadImageData, void *user_data);

  ///
  /// Set callback to use for writing image data
  ///
  void SetImageWriter(WriteImageDataFunction WriteImageData, void *user_data);

  ///
  /// Set callbacks to use for filesystem (fs) access and their user data
  ///
  void SetFsCallbacks(FsCallbacks callbacks);

 private:
  ///
  /// Loads glTF asset from string(memory).
  /// `length` = strlen(str);
  /// Set warning message to `warn` for example it fails to load asserts
  /// Returns false and set error string to `err` if there's an error.
  ///
  bool LoadFromString(Model *model, std::string *err, std::string *warn,
                      const char *str, const unsigned int length,
                      const std::string &base_dir, unsigned int check_sections);

  const unsigned char *bin_data_;
  size_t bin_size_;
  bool is_binary_;

  FsCallbacks fs = {
#ifndef TINYGLTF_NO_FS
      &tinygltf::FileExists, &tinygltf::ExpandFilePath,
      &tinygltf::ReadWholeFile, &tinygltf::WriteWholeFile,

      nullptr  // Fs callback user data
#else
      nullptr, nullptr, nullptr, nullptr,

      nullptr  // Fs callback user data
#endif
  };

  LoadImageDataFunction LoadImageData =
#ifndef TINYGLTF_NO_STB_IMAGE
      &tinygltf::LoadImageData;
#else
      nullptr;
#endif
  void *load_image_user_data_ = reinterpret_cast<void *>(&fs);

  WriteImageDataFunction WriteImageData =
#ifndef TINYGLTF_NO_STB_IMAGE_WRITE
      &tinygltf::WriteImageData;
#else
      nullptr;
#endif
  void *write_image_user_data_ = reinterpret_cast<void *>(&fs);
};

#ifdef __clang__
#pragma clang diagnostic pop  // -Wpadded
#endif

}  // namespace tinygltf

#endif  // TINY_GLTF_H_
