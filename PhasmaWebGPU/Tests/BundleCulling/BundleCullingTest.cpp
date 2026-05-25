// Port of webgpu-bundle-culling (github.com/toji/webgpu-bundle-culling) to
// PhasmaWebGPU's C++ sample harness. Locks in the upstream default preset:
//   renderMode        = renderBundleCulled
//   showPerspective   = true
//   showOverhead      = true
//   animateScene      = false
//   splitIndirectArgs = true (each drawable has its own 20B indirect buffer)
// No GUI. The compute cull pass and the two render bundles (perspective +
// overhead) are built once in Init and replayed every frame.
//
// Layout conventions match the inline WGSL in index.html:
//   group 0: CameraUniforms (VERTEX | FRAGMENT | COMPUTE)
//   group 1 (render): Material UBO
//   group 2 (render): { instances SSBO (read-only), culled SSBO (read-only) }
//   group 1 (compute): { instances SSBO (read-only), culled SSBO (rw),
//                        indirectArgs SSBO (rw) }
//
// The engine flips Y in the vertex shader (svPos.y = -y) so the
// CPU-computed frustum planes match upstream exactly.

#include "../Common/SampleApp.h"
#include "../Common/SampleBase.h"
#include "../Common/SampleUtils.h"
#include "../SampleShaderUtils.h"


#include <cmath>
#include <cstring>

namespace
{
    constexpr uint32_t kWidth = 1280;
    constexpr uint32_t kHeight = 720;
    constexpr float kPi = 3.14159265358979323846f;

    // Upstream: 1000 on desktop, 500 on mobile. We're desktop-only.
    constexpr uint32_t kMaxInstancesPerDrawable = 1000;
    // Upstream INSTANCE_ELEMENT_LENGTH (floats per instance mat4)
    constexpr uint32_t kInstanceFloatCount = 16;
    constexpr uint32_t kCullingWorkgroupSize = 64;
    constexpr uint32_t kSampleCount = 4;

    // CameraUniforms is 256B (64 f32). See tiny-webgpu-demo.js FRAME_BUFFER_SIZE.
    constexpr uint64_t kCameraUniformSize = 256;
    // Material UBO: vec4 color.
    constexpr uint64_t kMaterialSize = 16;
    // Indirect args: drawCount + instanceCount + firstIndex + 2x reserved = 5x u32.
    constexpr uint64_t kIndirectArgsSize = 20;

    constexpr float kCameraDistance = 3.0f;
    constexpr float kMinCameraDistance = 1.0f;
    constexpr float kMaxCameraDistance = 10.0f;
    constexpr float kOrbitDragScale = 0.025f;
    constexpr float kWheelDistanceStep = 0.6f;
    constexpr float kFov = kPi * 0.5f;
    constexpr float kZNear = 0.01f;
    constexpr float kZFar = 512.0f;

    struct MaterialColor
    {
        float r, g, b;
    };
    // Upstream creates 14 materials in index.html onInit().
    const MaterialColor kMaterialColors[] = {
        {1, 1, 1},
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, 1},
        {1, 1, 0},
        {1, 0, 1},
        {0, 1, 1},
        {0.5f, 0.5f, 0.5f},
        {0.5f, 0, 0},
        {0, 0.5f, 0},
        {0, 0, 0.5f},
        {0.5f, 0.5f, 0},
        {0.5f, 0, 0.5f},
        {0, 0.5f, 0.5f},
    };
    constexpr uint32_t kMaterialCount = sizeof(kMaterialColors) / sizeof(kMaterialColors[0]);

    // Deterministic JS-style Math.random() replacement. Upstream is not seeded
    // so visual output differs per run there; we seed to make the frustum /
    // cull work deterministic across our test runs.
    uint32_t g_rngState = 0xA57E401D;
    float FrandUnit()
    {
        // xorshift32 then map to [0,1).
        uint32_t x = g_rngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        g_rngState = x ? x : 0xA57E401D;
        return static_cast<float>(g_rngState & 0x00FFFFFFu) / 16777216.0f;
    }

    // ---------- geometry ----------

    // Vertex layout matches upstream DefaultAttribFormat for position/normal/uv:
    //   pos (3f) + normal (3f) + uv (2f) = 32B stride. Same as our RenderBundles
    //   port — see shapes.js.
    constexpr uint32_t kVertexStride = 8 * sizeof(float);

    struct CpuMesh
    {
        std::vector<float> vertices;   // 8 floats per vertex (pos, normal, uv)
        std::vector<uint16_t> indices; // may be empty (box path is indexless)
        bool hasIndices = false;
    };

    static void PushVertex(std::vector<float> &v, float px, float py, float pz,
                           float nx, float ny, float nz, float u, float vv)
    {
        v.push_back(px);
        v.push_back(py);
        v.push_back(pz);
        v.push_back(nx);
        v.push_back(ny);
        v.push_back(nz);
        v.push_back(u);
        v.push_back(vv);
    }

    // Port of shapes.js BoxGeometryDesc (width/height/depth = 1, centered).
    // Upstream writes 36 vertices with no index buffer.
    CpuMesh CreateBoxMesh()
    {
        CpuMesh m;
        const float w = 0.5f, h = 0.5f, d = 0.5f;
        const float x = 0.0f, y = 0.0f, z = 0.0f;

        // -Y
        PushVertex(m.vertices, x + w, y - h, z + d, 0, -1, 0, 1, 1);
        PushVertex(m.vertices, x - w, y - h, z + d, 0, -1, 0, 0, 1);
        PushVertex(m.vertices, x - w, y - h, z - d, 0, -1, 0, 0, 0);
        PushVertex(m.vertices, x + w, y - h, z - d, 0, -1, 0, 1, 0);
        PushVertex(m.vertices, x + w, y - h, z + d, 0, -1, 0, 1, 1);
        PushVertex(m.vertices, x - w, y - h, z - d, 0, -1, 0, 0, 0);
        // +X
        PushVertex(m.vertices, x + w, y + h, z + d, 1, 0, 0, 1, 1);
        PushVertex(m.vertices, x + w, y - h, z + d, 1, 0, 0, 0, 1);
        PushVertex(m.vertices, x + w, y - h, z - d, 1, 0, 0, 0, 0);
        PushVertex(m.vertices, x + w, y + h, z - d, 1, 0, 0, 1, 0);
        PushVertex(m.vertices, x + w, y + h, z + d, 1, 0, 0, 1, 1);
        PushVertex(m.vertices, x + w, y - h, z - d, 1, 0, 0, 0, 0);
        // +Y
        PushVertex(m.vertices, x - w, y + h, z + d, 0, 1, 0, 1, 1);
        PushVertex(m.vertices, x + w, y + h, z + d, 0, 1, 0, 0, 1);
        PushVertex(m.vertices, x + w, y + h, z - d, 0, 1, 0, 0, 0);
        PushVertex(m.vertices, x - w, y + h, z - d, 0, 1, 0, 1, 0);
        PushVertex(m.vertices, x - w, y + h, z + d, 0, 1, 0, 1, 1);
        PushVertex(m.vertices, x + w, y + h, z - d, 0, 1, 0, 0, 0);
        // -X
        PushVertex(m.vertices, x - w, y - h, z + d, -1, 0, 0, 1, 1);
        PushVertex(m.vertices, x - w, y + h, z + d, -1, 0, 0, 0, 1);
        PushVertex(m.vertices, x - w, y + h, z - d, -1, 0, 0, 0, 0);
        PushVertex(m.vertices, x - w, y - h, z - d, -1, 0, 0, 1, 0);
        PushVertex(m.vertices, x - w, y - h, z + d, -1, 0, 0, 1, 1);
        PushVertex(m.vertices, x - w, y + h, z - d, -1, 0, 0, 0, 0);
        // +Z
        PushVertex(m.vertices, x + w, y + h, z + d, 0, 0, 1, 1, 1);
        PushVertex(m.vertices, x - w, y + h, z + d, 0, 0, 1, 0, 1);
        PushVertex(m.vertices, x - w, y - h, z + d, 0, 0, 1, 0, 0);
        PushVertex(m.vertices, x - w, y - h, z + d, 0, 0, 1, 0, 0);
        PushVertex(m.vertices, x + w, y - h, z + d, 0, 0, 1, 1, 0);
        PushVertex(m.vertices, x + w, y + h, z + d, 0, 0, 1, 1, 1);
        // -Z
        PushVertex(m.vertices, x + w, y - h, z - d, 0, 0, -1, 1, 1);
        PushVertex(m.vertices, x - w, y - h, z - d, 0, 0, -1, 0, 1);
        PushVertex(m.vertices, x - w, y + h, z - d, 0, 0, -1, 0, 0);
        PushVertex(m.vertices, x + w, y + h, z - d, 0, 0, -1, 1, 0);
        PushVertex(m.vertices, x + w, y - h, z - d, 0, 0, -1, 1, 1);
        PushVertex(m.vertices, x - w, y + h, z - d, 0, 0, -1, 0, 0);
        return m;
    }

    // Port of shapes.js SphereGeometryDesc (radius=0.5, 32x16).
    CpuMesh CreateSphereMesh(float radius = 0.5f, int widthSegments = 32, int heightSegments = 16)
    {
        CpuMesh m;
        m.hasIndices = true;
        int index = 0;
        std::vector<std::vector<int>> grid;
        grid.reserve(static_cast<size_t>(heightSegments + 1));
        for (int iy = 0; iy <= heightSegments; ++iy)
        {
            std::vector<int> row;
            row.reserve(static_cast<size_t>(widthSegments + 1));
            const float v = static_cast<float>(iy) / static_cast<float>(heightSegments);

            float uOffset = 0.0f;
            if (iy == 0)
                uOffset = 0.5f / static_cast<float>(widthSegments);
            else if (iy == heightSegments)
                uOffset = -0.5f / static_cast<float>(widthSegments);

            for (int ix = 0; ix <= widthSegments; ++ix)
            {
                const float u = static_cast<float>(ix) / static_cast<float>(widthSegments);
                const float phi = u * 2.0f * kPi;
                const float theta = v * kPi;
                const float sp = std::sin(phi), cp = std::cos(phi);
                const float st = std::sin(theta), ct = std::cos(theta);
                const float vx = -radius * cp * st;
                const float vy = radius * ct;
                const float vz = radius * sp * st;

                float nx = vx, ny = vy, nz = vz;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 0.0f)
                {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }
                PushVertex(m.vertices, vx, vy, vz, nx, ny, nz, u + uOffset, 1.0f - v);
                row.push_back(index++);
            }
            grid.push_back(std::move(row));
        }

        for (int iy = 0; iy < heightSegments; ++iy)
        {
            for (int ix = 0; ix < widthSegments; ++ix)
            {
                const int a = grid[iy][ix + 1];
                const int b = grid[iy][ix];
                const int c = grid[iy + 1][ix];
                const int d = grid[iy + 1][ix + 1];
                if (iy != 0)
                {
                    m.indices.push_back(static_cast<uint16_t>(a));
                    m.indices.push_back(static_cast<uint16_t>(b));
                    m.indices.push_back(static_cast<uint16_t>(d));
                }
                if (iy != heightSegments - 1)
                {
                    m.indices.push_back(static_cast<uint16_t>(b));
                    m.indices.push_back(static_cast<uint16_t>(c));
                    m.indices.push_back(static_cast<uint16_t>(d));
                }
            }
        }
        return m;
    }

    // Port of shapes.js CylinderGeometryDesc (radiusTop=radiusBottom=0.5, h=1,
    // 32x1, closed). Capping matches upstream cap-per-face UV trick.
    CpuMesh CreateCylinderMeshShared(float radiusTop, float radiusBottom, float height,
                                     int radialSegments, int heightSegments)
    {
        CpuMesh m;
        m.hasIndices = true;
        int index = 0;
        const float halfHeight = height * 0.5f;

        // Torso
        std::vector<std::vector<int>> indexArray;
        indexArray.reserve(static_cast<size_t>(heightSegments + 1));
        const float slope = (radiusBottom - radiusTop) / height;
        for (int y = 0; y <= heightSegments; ++y)
        {
            std::vector<int> row;
            row.reserve(static_cast<size_t>(radialSegments + 1));
            const float v = static_cast<float>(y) / static_cast<float>(heightSegments);
            const float radius = v * (radiusBottom - radiusTop) + radiusTop;
            for (int x = 0; x <= radialSegments; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(radialSegments);
                const float theta = u * 2.0f * kPi;
                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);
                const float vx = radius * sinTheta;
                const float vy = -v * height + halfHeight;
                const float vz = radius * cosTheta;

                float nx = sinTheta, ny = slope, nz = cosTheta;
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 0.0f)
                {
                    nx /= len;
                    ny /= len;
                    nz /= len;
                }
                PushVertex(m.vertices, vx, vy, vz, nx, ny, nz, u, 1.0f - v);
                row.push_back(index++);
            }
            indexArray.push_back(std::move(row));
        }
        for (int x = 0; x < radialSegments; ++x)
        {
            for (int y = 0; y < heightSegments; ++y)
            {
                const int a = indexArray[y][x];
                const int b = indexArray[y + 1][x];
                const int c = indexArray[y + 1][x + 1];
                const int d = indexArray[y][x + 1];
                m.indices.push_back(static_cast<uint16_t>(a));
                m.indices.push_back(static_cast<uint16_t>(b));
                m.indices.push_back(static_cast<uint16_t>(d));
                m.indices.push_back(static_cast<uint16_t>(b));
                m.indices.push_back(static_cast<uint16_t>(c));
                m.indices.push_back(static_cast<uint16_t>(d));
            }
        }

        auto generateCap = [&](bool top)
        {
            const float radius = top ? radiusTop : radiusBottom;
            if (radius <= 0.0f)
                return;
            const float sign = top ? 1.0f : -1.0f;
            const int centerIndexStart = index;

            for (int x = 1; x <= radialSegments; ++x)
            {
                PushVertex(m.vertices, 0.0f, halfHeight * sign, 0.0f,
                           0.0f, sign, 0.0f, 0.5f, 0.5f);
                ++index;
            }
            const int centerIndexEnd = index;

            for (int x = 0; x <= radialSegments; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(radialSegments);
                const float theta = u * 2.0f * kPi;
                const float cosTheta = std::cos(theta);
                const float sinTheta = std::sin(theta);
                PushVertex(m.vertices,
                           radius * sinTheta, halfHeight * sign, radius * cosTheta,
                           0.0f, sign, 0.0f,
                           cosTheta * 0.5f + 0.5f, sinTheta * 0.5f * sign + 0.5f);
                ++index;
            }
            for (int x = 0; x < radialSegments; ++x)
            {
                const int c = centerIndexStart + x;
                const int i = centerIndexEnd + x;
                if (top)
                {
                    m.indices.push_back(static_cast<uint16_t>(i));
                    m.indices.push_back(static_cast<uint16_t>(i + 1));
                    m.indices.push_back(static_cast<uint16_t>(c));
                }
                else
                {
                    m.indices.push_back(static_cast<uint16_t>(i + 1));
                    m.indices.push_back(static_cast<uint16_t>(i));
                    m.indices.push_back(static_cast<uint16_t>(c));
                }
            }
        };
        generateCap(true);
        generateCap(false);
        return m;
    }

    CpuMesh CreateCylinderMesh()
    {
        return CreateCylinderMeshShared(0.5f, 0.5f, 1.0f, 32, 1);
    }

    CpuMesh CreateConeMesh()
    {
        // Matches ConeGeometryDesc: radiusTop=0, radiusBottom=options.radius
        // (defaulted to the cylinder's default 0.5 via ??).
        return CreateCylinderMeshShared(0.0f, 0.5f, 1.0f, 32, 1);
    }

    // GPU wrapper for a single mesh (VB + optional IB).
    struct GpuMesh
    {
        WGPUBuffer vertexBuffer = nullptr;
        uint64_t vertexBufferSize = 0;
        WGPUBuffer indexBuffer = nullptr;
        uint64_t indexBufferSize = 0;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0; // used when !hasIndices
        bool hasIndices = false;
    };

    static void ReleaseGpuMesh(GpuMesh &m)
    {
        if (m.vertexBuffer)
            wgpuBufferRelease(m.vertexBuffer);
        if (m.indexBuffer)
            wgpuBufferRelease(m.indexBuffer);
        m = GpuMesh{};
    }

    // Per-(material × geometry) drawable. Holds its own instance SSBO, culled
    // SSBO and 20B indirect-args buffer, plus two bind groups (one for the
    // render pass, one for the cull compute pass).
    struct Drawable
    {
        uint32_t materialIndex = 0;
        const GpuMesh *mesh = nullptr;

        WGPUBuffer instanceBuffer = nullptr;       // mat4[kMaxInstances]
        WGPUBuffer culledBuffer = nullptr;         // u32 indirectIndex + u32[kMaxInstances]
        WGPUBuffer indirectBuffer = nullptr;       // 20B draw args
        WGPUBindGroup instanceBindGroup = nullptr; // set=2 for render
        WGPUBindGroup cullBindGroup = nullptr;     // set=1 for compute
    };

    struct InstanceSeed
    {
        float pos[3];
        float scale[3];
        float axis[3];
        float rotationSpeed;
    };

    // --- Tiny glm-style helpers to avoid needing the full glm in this file.

    struct Mat4
    {
        float m[16]; // column-major
    };

    static Mat4 Mat4Identity()
    {
        Mat4 r{};
        r.m[0] = 1.0f;
        r.m[5] = 1.0f;
        r.m[10] = 1.0f;
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 Mat4Multiply(const Mat4 &a, const Mat4 &b)
    {
        Mat4 r{};
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k)
                    s += a.m[k * 4 + row] * b.m[col * 4 + k];
                r.m[col * 4 + row] = s;
            }
        }
        return r;
    }

    // gl-matrix perspectiveZO: right-handed, Z range [0, 1], OpenGL-style
    // (not Y-flipped). We do Y-flip in the shader, not here.
    static Mat4 Mat4PerspectiveZO(float fovy, float aspect, float zn, float zf)
    {
        const float f = 1.0f / std::tan(fovy * 0.5f);
        Mat4 r{};
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = zf / (zn - zf);
        r.m[11] = -1.0f;
        r.m[14] = (zf * zn) / (zn - zf);
        return r;
    }

    static Mat4 Mat4Translate(const Mat4 &src, float x, float y, float z)
    {
        Mat4 r = src;
        r.m[12] = src.m[0] * x + src.m[4] * y + src.m[8] * z + src.m[12];
        r.m[13] = src.m[1] * x + src.m[5] * y + src.m[9] * z + src.m[13];
        r.m[14] = src.m[2] * x + src.m[6] * y + src.m[10] * z + src.m[14];
        r.m[15] = src.m[3] * x + src.m[7] * y + src.m[11] * z + src.m[15];
        return r;
    }

    static Mat4 Mat4RotateX(const Mat4 &src, float rad)
    {
        const float s = std::sin(rad), c = std::cos(rad);
        Mat4 r = src;
        const float a10 = src.m[4], a11 = src.m[5], a12 = src.m[6], a13 = src.m[7];
        const float a20 = src.m[8], a21 = src.m[9], a22 = src.m[10], a23 = src.m[11];
        r.m[4] = a10 * c + a20 * s;
        r.m[5] = a11 * c + a21 * s;
        r.m[6] = a12 * c + a22 * s;
        r.m[7] = a13 * c + a23 * s;
        r.m[8] = a20 * c - a10 * s;
        r.m[9] = a21 * c - a11 * s;
        r.m[10] = a22 * c - a12 * s;
        r.m[11] = a23 * c - a13 * s;
        return r;
    }

    static Mat4 Mat4RotateY(const Mat4 &src, float rad)
    {
        const float s = std::sin(rad), c = std::cos(rad);
        Mat4 r = src;
        const float a00 = src.m[0], a01 = src.m[1], a02 = src.m[2], a03 = src.m[3];
        const float a20 = src.m[8], a21 = src.m[9], a22 = src.m[10], a23 = src.m[11];
        r.m[0] = a00 * c - a20 * s;
        r.m[1] = a01 * c - a21 * s;
        r.m[2] = a02 * c - a22 * s;
        r.m[3] = a03 * c - a23 * s;
        r.m[8] = a00 * s + a20 * c;
        r.m[9] = a01 * s + a21 * c;
        r.m[10] = a02 * s + a22 * c;
        r.m[11] = a03 * s + a23 * c;
        return r;
    }

    // gl-matrix fromRotationTranslationScale: T * R * S (assembles a model
    // matrix from quat + pos + scale). Quat is {x, y, z, w}.
    static Mat4 Mat4FromRotationTranslationScale(const float q[4], const float t[3], const float s[3])
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float x2 = x + x, y2 = y + y, z2 = z + z;
        const float xx = x * x2, xy = x * y2, xz = x * z2;
        const float yy = y * y2, yz = y * z2, zz = z * z2;
        const float wx = w * x2, wy = w * y2, wz = w * z2;
        const float sx = s[0], sy = s[1], sz = s[2];

        Mat4 r{};
        r.m[0] = (1.0f - (yy + zz)) * sx;
        r.m[1] = (xy + wz) * sx;
        r.m[2] = (xz - wy) * sx;
        r.m[3] = 0.0f;
        r.m[4] = (xy - wz) * sy;
        r.m[5] = (1.0f - (xx + zz)) * sy;
        r.m[6] = (yz + wx) * sy;
        r.m[7] = 0.0f;
        r.m[8] = (xz + wy) * sz;
        r.m[9] = (yz - wx) * sz;
        r.m[10] = (1.0f - (xx + yy)) * sz;
        r.m[11] = 0.0f;
        r.m[12] = t[0];
        r.m[13] = t[1];
        r.m[14] = t[2];
        r.m[15] = 1.0f;
        return r;
    }

    // gl-matrix quat setAxisAngle(axis, angle).
    static void QuatFromAxisAngle(float outQ[4], const float axis[3], float rad)
    {
        const float half = rad * 0.5f;
        const float s = std::sin(half);
        outQ[0] = axis[0] * s;
        outQ[1] = axis[1] * s;
        outQ[2] = axis[2] * s;
        outQ[3] = std::cos(half);
    }

    static float Vec3Length(float x, float y, float z)
    {
        return std::sqrt(x * x + y * y + z * z);
    }

    // ---------- orbit camera view matrix ----------

    static float WrapPi(float angle)
    {
        while (angle < -kPi)
            angle += 2.0f * kPi;
        while (angle >= kPi)
            angle -= 2.0f * kPi;
        return angle;
    }

    static Mat4 BuildOrbitCameraMatrix(float orbitX, float orbitY, float distance)
    {
        Mat4 camera = Mat4Identity();
        camera = Mat4RotateY(camera, -orbitY);
        camera = Mat4RotateX(camera, -orbitX);
        camera = Mat4Translate(camera, 0.0f, 0.0f, distance);
        return camera;
    }

    static Mat4 BuildOrbitView(float orbitX, float orbitY, float distance)
    {
        // Upstream view = inverse(T(target) * Ry(-orbitY) * Rx(-orbitX) * T(distance)).
        Mat4 v = Mat4Identity();
        v = Mat4Translate(v, 0.0f, 0.0f, -distance);
        v = Mat4RotateX(v, orbitX);
        v = Mat4RotateY(v, orbitY);
        return v;
    }

    // Upstream updateOverheadView: identity * translate(0,0,-200) * rotateX(pi/2).
    static Mat4 BuildOverheadView()
    {
        Mat4 v = Mat4Identity();
        v = Mat4Translate(v, 0.0f, 0.0f, -200.0f);
        v = Mat4RotateX(v, kPi * 0.5f);
        return v;
    }

    // Gribb/Hartmann frustum extraction from a row-major multiplied matrix,
    // same approach as tiny-webgpu-demo.buildFrustum — except gl-matrix is
    // column-major so we index into the column-major projection*view product
    // using the upstream indices.
    static void BuildFrustum(const Mat4 &projection, const Mat4 &view, float outFrustum[6 * 4])
    {
        Mat4 mp = Mat4Multiply(projection, view);
        const float *mat = mp.m; // column-major

        auto normalizePlane = [&](int planeIdx, float x, float y, float z, float w)
        {
            const float l = Vec3Length(x, y, z);
            outFrustum[planeIdx * 4 + 0] = x / l;
            outFrustum[planeIdx * 4 + 1] = y / l;
            outFrustum[planeIdx * 4 + 2] = z / l;
            outFrustum[planeIdx * 4 + 3] = w / l;
        };

        // mat[3], mat[7], mat[11], mat[15] are the w-row (indices 3,7,11,15
        // in column-major), matching the gl-matrix convention used in
        // tiny-webgpu-demo.js.
        const float m0 = mat[0], m1 = mat[1], m2 = mat[2], m3 = mat[3];
        const float m4 = mat[4], m5 = mat[5], m6 = mat[6], m7 = mat[7];
        const float m8 = mat[8], m9 = mat[9], m10 = mat[10], m11 = mat[11];
        const float m12 = mat[12], m13 = mat[13], m14 = mat[14], m15 = mat[15];

        // Left
        normalizePlane(0, m3 + m0, m7 + m4, m11 + m8, m15 + m12);
        // Right
        normalizePlane(1, m3 - m0, m7 - m4, m11 - m8, m15 - m12);
        // Top
        normalizePlane(2, m3 - m1, m7 - m5, m11 - m9, m15 - m13);
        // Bottom
        normalizePlane(3, m3 + m1, m7 + m5, m11 + m9, m15 + m13);
        // Near
        normalizePlane(4, m2, m6, m10, m14);
        // Far
        normalizePlane(5, m3 - m2, m7 - m6, m11 - m10, m15 - m14);
    }

    // ---------- the sample ----------

    class BundleCullingSample final : public pwgpu::test::SampleBase
    {
    public:
        bool Init(pwgpu::test::SampleContext &ctx) override
        {
            m_vsNormal = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "geometry.vert.hlsl").c_str(), "geom_vs");
            m_vsCulled = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "geometryCulled.vert.hlsl").c_str(), "geom_culled_vs");
            m_ps = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "geometry.pixel.hlsl").c_str(), "geom_ps");
            m_cullCs = pwgpu::test::MakeRuntimeShaderModule(
                ctx.device, (ctx.shaderDir + "cull.comp.hlsl").c_str(), "cull_cs");
            if (!m_vsNormal || !m_vsCulled || !m_ps || !m_cullCs)
                return false;

            if (!CreateFrameResources(ctx))
                return false;
            if (!CreateMaterialResources(ctx))
                return false;
            if (!CreateMeshes(ctx))
                return false;
            if (!CreatePipelines(ctx))
                return false;
            if (!CreateDrawables(ctx))
                return false;

            Resize(ctx, ctx.width, ctx.height);
            if (!m_depthView)
                return false;

            if (!BuildBundles(ctx))
                return false;

            fprintf(stdout,
                    "[BundleCulling] %u materials x %u geometries = %zu drawables,"
                    " %u instances each (total %zu)\n",
                    kMaterialCount,
                    static_cast<uint32_t>(m_meshes.size()),
                    m_drawables.size(),
                    kMaxInstancesPerDrawable,
                    m_drawables.size() * kMaxInstancesPerDrawable);
            return true;
        }

        void Resize(pwgpu::test::SampleContext &ctx, uint32_t width, uint32_t height) override
        {
            ReleaseRenderTargets();
            if (width == 0 || height == 0)
                return;

            WGPUTextureDescriptor colorDesc{};
            colorDesc.label = {"bc_msaa_color", WGPU_STRLEN};
            colorDesc.dimension = WGPUTextureDimension_2D;
            colorDesc.size = {width, height, 1};
            colorDesc.mipLevelCount = 1;
            colorDesc.sampleCount = kSampleCount;
            colorDesc.format = ctx.surfaceFormat;
            colorDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_msaaColorTexture = wgpuDeviceCreateTexture(ctx.device, &colorDesc);
            if (!m_msaaColorTexture)
                return;

            m_msaaColorView = pwgpu::test::CreateTextureView(m_msaaColorTexture, ctx.surfaceFormat);
            if (!m_msaaColorView)
                return;

            WGPUTextureDescriptor depthDesc{};
            depthDesc.label = {"bc_depth", WGPU_STRLEN};
            depthDesc.dimension = WGPUTextureDimension_2D;
            depthDesc.size = {width, height, 1};
            depthDesc.mipLevelCount = 1;
            depthDesc.sampleCount = kSampleCount;
            depthDesc.format = WGPUTextureFormat_Depth24Plus;
            depthDesc.usage = WGPUTextureUsage_RenderAttachment;
            m_depthTexture = wgpuDeviceCreateTexture(ctx.device, &depthDesc);
            if (!m_depthTexture)
                return;

            WGPUTextureViewDescriptor viewDesc{};
            viewDesc.format = WGPUTextureFormat_Depth24Plus;
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.mipLevelCount = 1;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_All;
            m_depthView = wgpuTextureCreateView(m_depthTexture, &viewDesc);
        }

        void HandleEvent(pwgpu::test::SampleContext &ctx,
                         const SDL_Event &event,
                         bool &running) override
        {
            pwgpu::test::SampleBase::HandleEvent(ctx, event, running);

            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                m_orbiting = true;
                m_lastMouseX = event.button.x;
                m_lastMouseY = event.button.y;
                return;
            }

            if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT)
            {
                m_orbiting = false;
                return;
            }

            if (event.type == SDL_MOUSEMOTION && m_orbiting)
            {
                const int dx = event.motion.x - m_lastMouseX;
                const int dy = event.motion.y - m_lastMouseY;
                m_lastMouseX = event.motion.x;
                m_lastMouseY = event.motion.y;

                m_orbitY = WrapPi(m_orbitY + static_cast<float>(dx) * kOrbitDragScale);
                m_orbitX = std::clamp(m_orbitX + static_cast<float>(dy) * kOrbitDragScale,
                                      -kPi * 0.5f,
                                      kPi * 0.5f);
                return;
            }

            if (event.type == SDL_MOUSEWHEEL)
            {
                m_cameraDistance =
                    std::clamp(m_cameraDistance -
                                   static_cast<float>(event.wheel.y) * kWheelDistanceStep,
                               kMinCameraDistance,
                               kMaxCameraDistance);
            }
        }

        void Update(pwgpu::test::SampleContext &ctx) override
        {
            // Half-resolution viewport for each half when showOverhead && showPerspective.
            // Upstream updateProjection splits horizontally because width >= height here.
            const float halfW = static_cast<float>(ctx.width) * 0.5f;
            const float h = static_cast<float>(ctx.height);
            const float aspect = h > 0.0f ? halfW / h : 1.0f;

            m_projection = Mat4PerspectiveZO(kFov, aspect, kZNear, kZFar);
            m_view = BuildOrbitView(m_orbitX, m_orbitY, m_cameraDistance);
            m_overheadView = BuildOverheadView();
            const Mat4 cameraMatrix = BuildOrbitCameraMatrix(m_orbitX, m_orbitY, m_cameraDistance);
            m_cameraPosition[0] = cameraMatrix.m[12];
            m_cameraPosition[1] = cameraMatrix.m[13];
            m_cameraPosition[2] = cameraMatrix.m[14];

            WriteFrameUniform(ctx, m_frameUniformBuffer, m_projection, m_view, m_cameraPosition);
            WriteFrameUniform(ctx,
                              m_overheadFrameUniformBuffer,
                              m_projection,
                              m_overheadView,
                              m_cameraPosition);
        }

        bool Execute(pwgpu::test::SampleContext &ctx, pwgpu::test::SampleFrame &frame) override
        {
            if (!m_pipelineCulled || !m_cullPipeline || !m_depthView || !m_msaaColorView ||
                !m_bundlePerspective || !m_bundleOverhead)
                return true;

            // Cull pass for the perspective camera. The overhead view in this
            // demo is chiefly meant to visualise the frustum culling of the
            // perspective camera (per upstream), so both bundles read the same
            // culled-indices lists produced by this dispatch.
            CullInstances(frame);

            WGPURenderPassColorAttachment colorAttachment{};
            colorAttachment.view = m_msaaColorView;
            colorAttachment.resolveTarget = frame.surfaceView;
            colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Discard;
            colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

            WGPURenderPassDepthStencilAttachment depthAttachment{};
            depthAttachment.view = m_depthView;
            depthAttachment.depthClearValue = 1.0f;
            depthAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthAttachment.depthStoreOp = WGPUStoreOp_Store;
            depthAttachment.depthReadOnly = WGPUOptionalBool_False;
            depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
            depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
            depthAttachment.stencilReadOnly = WGPUOptionalBool_True;

            WGPURenderPassDescriptor passDesc{};
            passDesc.label = {"bc_pass", WGPU_STRLEN};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;
            passDesc.depthStencilAttachment = &depthAttachment;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &passDesc);

            const float fullW = static_cast<float>(ctx.width);
            const float fullH = static_cast<float>(ctx.height);
            const float halfW = fullW * 0.5f;

            // Perspective on the left half.
            wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f, halfW, fullH, 0.0f, 1.0f);
            wgpuRenderPassEncoderExecuteBundles(pass, 1, &m_bundlePerspective);

            // Overhead on the right half.
            wgpuRenderPassEncoderSetViewport(pass, halfW, 0.0f, halfW, fullH, 0.0f, 1.0f);
            wgpuRenderPassEncoderExecuteBundles(pass, 1, &m_bundleOverhead);

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            return true;
        }

        void Shutdown(pwgpu::test::SampleContext &) override
        {
            ReleaseRenderTargets();

            if (m_bundlePerspective)
                wgpuRenderBundleRelease(m_bundlePerspective);
            if (m_bundleOverhead)
                wgpuRenderBundleRelease(m_bundleOverhead);

            for (Drawable &d : m_drawables)
            {
                if (d.cullBindGroup)
                    wgpuBindGroupRelease(d.cullBindGroup);
                if (d.instanceBindGroup)
                    wgpuBindGroupRelease(d.instanceBindGroup);
                if (d.indirectBuffer)
                    wgpuBufferRelease(d.indirectBuffer);
                if (d.culledBuffer)
                    wgpuBufferRelease(d.culledBuffer);
                if (d.instanceBuffer)
                    wgpuBufferRelease(d.instanceBuffer);
            }
            m_drawables.clear();

            for (GpuMesh &m : m_meshes)
                ReleaseGpuMesh(m);
            m_meshes.clear();

            for (WGPUBindGroup bg : m_materialBindGroups)
                if (bg)
                    wgpuBindGroupRelease(bg);
            m_materialBindGroups.clear();
            for (WGPUBuffer b : m_materialBuffers)
                if (b)
                    wgpuBufferRelease(b);
            m_materialBuffers.clear();

            if (m_pipelineCulled)
                wgpuRenderPipelineRelease(m_pipelineCulled);
            if (m_pipelineNaive)
                wgpuRenderPipelineRelease(m_pipelineNaive);
            if (m_cullPipeline)
                wgpuComputePipelineRelease(m_cullPipeline);
            if (m_renderPipelineLayout)
                wgpuPipelineLayoutRelease(m_renderPipelineLayout);
            if (m_cullPipelineLayout)
                wgpuPipelineLayoutRelease(m_cullPipelineLayout);

            if (m_instanceBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_instanceBindGroupLayout);
            if (m_cullBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_cullBindGroupLayout);
            if (m_materialBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_materialBindGroupLayout);
            if (m_frameBindGroup)
                wgpuBindGroupRelease(m_frameBindGroup);
            if (m_overheadFrameBindGroup)
                wgpuBindGroupRelease(m_overheadFrameBindGroup);
            if (m_frameBindGroupLayout)
                wgpuBindGroupLayoutRelease(m_frameBindGroupLayout);
            if (m_frameUniformBuffer)
                wgpuBufferRelease(m_frameUniformBuffer);
            if (m_overheadFrameUniformBuffer)
                wgpuBufferRelease(m_overheadFrameUniformBuffer);

            if (m_cullCs)
                wgpuShaderModuleRelease(m_cullCs);
            if (m_ps)
                wgpuShaderModuleRelease(m_ps);
            if (m_vsCulled)
                wgpuShaderModuleRelease(m_vsCulled);
            if (m_vsNormal)
                wgpuShaderModuleRelease(m_vsNormal);
        }

    private:
        bool CreateFrameResources(pwgpu::test::SampleContext &ctx)
        {
            m_frameUniformBuffer =
                pwgpu::test::CreateBuffer(ctx.device, kCameraUniformSize,
                                          WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                          "bc_frame_ub");
            m_overheadFrameUniformBuffer =
                pwgpu::test::CreateBuffer(ctx.device, kCameraUniformSize,
                                          WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                          "bc_overhead_ub");
            if (!m_frameUniformBuffer || !m_overheadFrameUniformBuffer)
                return false;

            WGPUBindGroupLayoutEntry frameEntry{};
            frameEntry.binding = 0;
            frameEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment |
                                    WGPUShaderStage_Compute;
            frameEntry.buffer.type = WGPUBufferBindingType_Uniform;
            frameEntry.buffer.minBindingSize = kCameraUniformSize;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"bc_frame_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 1;
            bglDesc.entries = &frameEntry;
            m_frameBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_frameBindGroupLayout)
                return false;

            auto makeFrameBg = [&](WGPUBuffer buf, const char *label) -> WGPUBindGroup
            {
                WGPUBindGroupEntry e{};
                e.binding = 0;
                e.buffer = buf;
                e.size = kCameraUniformSize;
                WGPUBindGroupDescriptor desc{};
                desc.label = {label, WGPU_STRLEN};
                desc.layout = m_frameBindGroupLayout;
                desc.entryCount = 1;
                desc.entries = &e;
                return wgpuDeviceCreateBindGroup(ctx.device, &desc);
            };
            m_frameBindGroup = makeFrameBg(m_frameUniformBuffer, "bc_frame_bg");
            m_overheadFrameBindGroup = makeFrameBg(m_overheadFrameUniformBuffer, "bc_overhead_bg");
            return m_frameBindGroup && m_overheadFrameBindGroup;
        }

        bool CreateMaterialResources(pwgpu::test::SampleContext &ctx)
        {
            WGPUBindGroupLayoutEntry mEntry{};
            mEntry.binding = 0;
            mEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            mEntry.buffer.type = WGPUBufferBindingType_Uniform;
            mEntry.buffer.minBindingSize = kMaterialSize;

            WGPUBindGroupLayoutDescriptor bglDesc{};
            bglDesc.label = {"bc_mat_bgl", WGPU_STRLEN};
            bglDesc.entryCount = 1;
            bglDesc.entries = &mEntry;
            m_materialBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &bglDesc);
            if (!m_materialBindGroupLayout)
                return false;

            m_materialBuffers.resize(kMaterialCount, nullptr);
            m_materialBindGroups.resize(kMaterialCount, nullptr);
            for (uint32_t i = 0; i < kMaterialCount; ++i)
            {
                char label[32];
                std::snprintf(label, sizeof(label), "bc_mat_%u", i);
                m_materialBuffers[i] = pwgpu::test::CreateBuffer(
                    ctx.device, kMaterialSize,
                    WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, label);
                if (!m_materialBuffers[i])
                    return false;
                const float data[4] = {kMaterialColors[i].r, kMaterialColors[i].g,
                                       kMaterialColors[i].b, 1.0f};
                wgpuQueueWriteBuffer(ctx.queue, m_materialBuffers[i], 0, data, sizeof(data));

                WGPUBindGroupEntry e{};
                e.binding = 0;
                e.buffer = m_materialBuffers[i];
                e.size = kMaterialSize;
                WGPUBindGroupDescriptor desc{};
                desc.label = {label, WGPU_STRLEN};
                desc.layout = m_materialBindGroupLayout;
                desc.entryCount = 1;
                desc.entries = &e;
                m_materialBindGroups[i] = wgpuDeviceCreateBindGroup(ctx.device, &desc);
                if (!m_materialBindGroups[i])
                    return false;
            }
            return true;
        }

        bool UploadMesh(pwgpu::test::SampleContext &ctx, const CpuMesh &cpu,
                        const char *label, GpuMesh &out)
        {
            const uint64_t vbBytes = static_cast<uint64_t>(cpu.vertices.size()) * sizeof(float);
            out.vertexBufferSize = vbBytes;
            out.vertexCount = static_cast<uint32_t>(cpu.vertices.size() / 8);
            out.vertexBuffer = pwgpu::test::CreateBuffer(
                ctx.device, vbBytes,
                WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, label);
            if (!out.vertexBuffer)
                return false;
            wgpuQueueWriteBuffer(ctx.queue, out.vertexBuffer, 0, cpu.vertices.data(), vbBytes);

            out.hasIndices = cpu.hasIndices;
            if (cpu.hasIndices)
            {
                const uint64_t rawIbBytes =
                    static_cast<uint64_t>(cpu.indices.size()) * sizeof(uint16_t);
                const uint64_t ibBytes = (rawIbBytes + 3u) & ~uint64_t(3);
                out.indexBufferSize = ibBytes;
                out.indexCount = static_cast<uint32_t>(cpu.indices.size());
                out.indexBuffer = pwgpu::test::CreateBuffer(
                    ctx.device, ibBytes,
                    WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst, label);
                if (!out.indexBuffer)
                    return false;
                std::vector<uint8_t> padded(static_cast<size_t>(ibBytes), 0);
                std::memcpy(padded.data(), cpu.indices.data(),
                            static_cast<size_t>(rawIbBytes));
                wgpuQueueWriteBuffer(ctx.queue, out.indexBuffer, 0, padded.data(), ibBytes);
            }
            return true;
        }

        bool CreateMeshes(pwgpu::test::SampleContext &ctx)
        {
            m_meshes.resize(4);
            if (!UploadMesh(ctx, CreateBoxMesh(), "bc_box", m_meshes[0]))
                return false;
            if (!UploadMesh(ctx, CreateSphereMesh(), "bc_sphere", m_meshes[1]))
                return false;
            if (!UploadMesh(ctx, CreateCylinderMesh(), "bc_cylinder", m_meshes[2]))
                return false;
            if (!UploadMesh(ctx, CreateConeMesh(), "bc_cone", m_meshes[3]))
                return false;
            return true;
        }

        bool CreatePipelines(pwgpu::test::SampleContext &ctx)
        {
            // Instance bind group layout: { instances (ro-storage, VS),
            //                               culled (ro-storage, VS) }
            WGPUBindGroupLayoutEntry instEntries[2] = {};
            instEntries[0].binding = 0;
            instEntries[0].visibility = WGPUShaderStage_Vertex;
            instEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            instEntries[1].binding = 1;
            instEntries[1].visibility = WGPUShaderStage_Vertex;
            instEntries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

            WGPUBindGroupLayoutDescriptor instBgl{};
            instBgl.label = {"bc_inst_bgl", WGPU_STRLEN};
            instBgl.entryCount = 2;
            instBgl.entries = instEntries;
            m_instanceBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &instBgl);
            if (!m_instanceBindGroupLayout)
                return false;

            // Cull bind group layout: { instances (ro-storage), culled (rw-storage),
            //                           indirectArgs (rw-storage) }
            WGPUBindGroupLayoutEntry cullEntries[3] = {};
            cullEntries[0].binding = 0;
            cullEntries[0].visibility = WGPUShaderStage_Compute;
            cullEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            cullEntries[1].binding = 1;
            cullEntries[1].visibility = WGPUShaderStage_Compute;
            cullEntries[1].buffer.type = WGPUBufferBindingType_Storage;
            cullEntries[2].binding = 2;
            cullEntries[2].visibility = WGPUShaderStage_Compute;
            cullEntries[2].buffer.type = WGPUBufferBindingType_Storage;

            WGPUBindGroupLayoutDescriptor cullBgl{};
            cullBgl.label = {"bc_cull_bgl", WGPU_STRLEN};
            cullBgl.entryCount = 3;
            cullBgl.entries = cullEntries;
            m_cullBindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device, &cullBgl);
            if (!m_cullBindGroupLayout)
                return false;

            WGPUBindGroupLayout renderLayouts[3] = {
                m_frameBindGroupLayout, m_materialBindGroupLayout, m_instanceBindGroupLayout};
            WGPUPipelineLayoutDescriptor renderPlDesc{};
            renderPlDesc.label = {"bc_render_pl", WGPU_STRLEN};
            renderPlDesc.bindGroupLayoutCount = 3;
            renderPlDesc.bindGroupLayouts = renderLayouts;
            m_renderPipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &renderPlDesc);
            if (!m_renderPipelineLayout)
                return false;

            WGPUBindGroupLayout cullLayouts[2] = {m_frameBindGroupLayout, m_cullBindGroupLayout};
            WGPUPipelineLayoutDescriptor cullPlDesc{};
            cullPlDesc.label = {"bc_cull_pl", WGPU_STRLEN};
            cullPlDesc.bindGroupLayoutCount = 2;
            cullPlDesc.bindGroupLayouts = cullLayouts;
            m_cullPipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device, &cullPlDesc);
            if (!m_cullPipelineLayout)
                return false;

            WGPUComputePipelineDescriptor cpDesc{};
            cpDesc.label = {"bc_cull_cp", WGPU_STRLEN};
            cpDesc.layout = m_cullPipelineLayout;
            cpDesc.compute.module = m_cullCs;
            cpDesc.compute.entryPoint = {"CSMain", WGPU_STRLEN};
            m_cullPipeline = wgpuDeviceCreateComputePipeline(ctx.device, &cpDesc);
            if (!m_cullPipeline)
                return false;

            WGPUVertexAttribute attributes[3] = {};
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = 0;
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x3;
            attributes[1].offset = 3 * sizeof(float);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WGPUVertexFormat_Float32x2;
            attributes[2].offset = 6 * sizeof(float);
            attributes[2].shaderLocation = 2;

            WGPUVertexBufferLayout vbLayout{};
            vbLayout.arrayStride = kVertexStride;
            vbLayout.stepMode = WGPUVertexStepMode_Vertex;
            vbLayout.attributeCount = 3;
            vbLayout.attributes = attributes;

            WGPUColorTargetState colorTarget{};
            colorTarget.format = ctx.surfaceFormat;
            colorTarget.writeMask = WGPUColorWriteMask_All;

            WGPUDepthStencilState depthState{};
            depthState.format = WGPUTextureFormat_Depth24Plus;
            depthState.depthWriteEnabled = WGPUOptionalBool_True;
            depthState.depthCompare = WGPUCompareFunction_LessEqual;
            depthState.stencilFront.compare = WGPUCompareFunction_Always;
            depthState.stencilFront.failOp = WGPUStencilOperation_Keep;
            depthState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
            depthState.stencilFront.passOp = WGPUStencilOperation_Keep;
            depthState.stencilBack = depthState.stencilFront;

            auto makePipeline = [&](WGPUShaderModule vs, const char *label) -> WGPURenderPipeline
            {
                WGPUFragmentState fragmentState{};
                fragmentState.module = m_ps;
                fragmentState.entryPoint = {"PSMain", WGPU_STRLEN};
                fragmentState.targetCount = 1;
                fragmentState.targets = &colorTarget;

                WGPURenderPipelineDescriptor desc{};
                desc.label = {label, WGPU_STRLEN};
                desc.layout = m_renderPipelineLayout;
                desc.vertex.module = vs;
                desc.vertex.entryPoint = {"VSMain", WGPU_STRLEN};
                desc.vertex.bufferCount = 1;
                desc.vertex.buffers = &vbLayout;
                desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                desc.primitive.cullMode = WGPUCullMode_None;
                desc.primitive.frontFace = WGPUFrontFace_CCW;
                desc.multisample.count = kSampleCount;
                desc.multisample.mask = 0xFFFFFFFF;
                desc.depthStencil = &depthState;
                desc.fragment = &fragmentState;
                return wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
            };

            m_pipelineNaive = makePipeline(m_vsNormal, "bc_pipe_naive");
            m_pipelineCulled = makePipeline(m_vsCulled, "bc_pipe_culled");
            return m_pipelineNaive && m_pipelineCulled;
        }

        bool CreateDrawables(pwgpu::test::SampleContext &ctx)
        {
            g_rngState = 0xA57E401D;

            const uint32_t meshCount = static_cast<uint32_t>(m_meshes.size());
            const uint32_t drawableCount = kMaterialCount * meshCount;
            m_drawables.resize(drawableCount);

            const uint64_t instanceBytes =
                static_cast<uint64_t>(kMaxInstancesPerDrawable) * kInstanceFloatCount * sizeof(float);
            const uint64_t culledBytes =
                static_cast<uint64_t>(kMaxInstancesPerDrawable) * sizeof(uint32_t) + 4ull;

            std::vector<float> scratch(
                static_cast<size_t>(kMaxInstancesPerDrawable) * kInstanceFloatCount);
            std::vector<InstanceSeed> seeds(kMaxInstancesPerDrawable);

            uint32_t drawIdx = 0;
            for (uint32_t mi = 0; mi < kMaterialCount; ++mi)
            {
                for (uint32_t gi = 0; gi < meshCount; ++gi, ++drawIdx)
                {
                    Drawable &d = m_drawables[drawIdx];
                    d.materialIndex = mi;
                    d.mesh = &m_meshes[gi];

                    // Populate per-instance seeds.
                    for (uint32_t i = 0; i < kMaxInstancesPerDrawable; ++i)
                    {
                        const float scale = FrandUnit() + 0.5f;
                        float axisX = FrandUnit() * 2.0f - 1.0f;
                        float axisY = FrandUnit() * 2.0f - 1.0f;
                        float axisZ = FrandUnit() * 2.0f - 1.0f;
                        float al = Vec3Length(axisX, axisY, axisZ);
                        if (al < 1e-6f)
                        {
                            axisX = 1.0f;
                            axisY = 0.0f;
                            axisZ = 0.0f;
                            al = 1.0f;
                        }
                        seeds[i].pos[0] = (FrandUnit() * 2.0f - 1.0f) * 100.0f;
                        seeds[i].pos[1] = (FrandUnit() * 2.0f - 1.0f) * 100.0f;
                        seeds[i].pos[2] = (FrandUnit() * 2.0f - 1.0f) * 100.0f;
                        seeds[i].scale[0] = scale;
                        seeds[i].scale[1] = scale;
                        seeds[i].scale[2] = scale;
                        seeds[i].axis[0] = axisX / al;
                        seeds[i].axis[1] = axisY / al;
                        seeds[i].axis[2] = axisZ / al;
                        seeds[i].rotationSpeed = FrandUnit() * 2.0f - 1.0f;
                    }

                    // Sort by distance from origin, nearest first, to match
                    // upstream overdraw ordering.
                    std::sort(seeds.begin(), seeds.end(),
                              [](const InstanceSeed &a, const InstanceSeed &b)
                              {
                                  const float la = Vec3Length(a.pos[0], a.pos[1], a.pos[2]);
                                  const float lb = Vec3Length(b.pos[0], b.pos[1], b.pos[2]);
                                  return la < lb;
                              });

                    // Build static model matrices at t=0 (no animation in the
                    // locked preset). Each instance gets a rotation around its
                    // axis by angle 0 — identity quaternion — so model = T * S.
                    // We still go through fromRotationTranslationScale with a
                    // unit quat to match upstream's code path.
                    const float identQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                    for (uint32_t i = 0; i < kMaxInstancesPerDrawable; ++i)
                    {
                        Mat4 mm = Mat4FromRotationTranslationScale(
                            identQuat, seeds[i].pos, seeds[i].scale);
                        std::memcpy(&scratch[i * kInstanceFloatCount],
                                    mm.m, sizeof(float) * 16);
                    }

                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "bc_inst_%u", drawIdx);
                    d.instanceBuffer = pwgpu::test::CreateBuffer(
                        ctx.device, instanceBytes,
                        WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, buf);
                    if (!d.instanceBuffer)
                        return false;
                    wgpuQueueWriteBuffer(ctx.queue, d.instanceBuffer, 0,
                                         scratch.data(), instanceBytes);

                    std::snprintf(buf, sizeof(buf), "bc_culled_%u", drawIdx);
                    d.culledBuffer = pwgpu::test::CreateBuffer(
                        ctx.device, culledBytes,
                        WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, buf);
                    if (!d.culledBuffer)
                        return false;
                    // Split-indirect mode binds one 20B indirect command per drawable, so
                    // culled.indirectIndex is always zero inside that bound buffer. The
                    // unsplit upstream path uses drawIdx here for a shared indirect array.
                    const uint32_t indirectIndex = 0;
                    wgpuQueueWriteBuffer(ctx.queue, d.culledBuffer, 0,
                                         &indirectIndex, sizeof(uint32_t));

                    std::snprintf(buf, sizeof(buf), "bc_indirect_%u", drawIdx);
                    d.indirectBuffer = pwgpu::test::CreateBuffer(
                        ctx.device, kIndirectArgsSize,
                        WGPUBufferUsage_Indirect | WGPUBufferUsage_Storage |
                            WGPUBufferUsage_CopyDst,
                        buf);
                    if (!d.indirectBuffer)
                        return false;
                    uint32_t initialArgs[5] = {0, 0, 0, 0, 0};
                    if (d.mesh->hasIndices)
                    {
                        initialArgs[0] = d.mesh->indexCount;
                    }
                    else
                    {
                        initialArgs[0] = d.mesh->vertexCount;
                    }
                    initialArgs[1] = 0; // instanceCount, bumped by compute
                    initialArgs[2] = 0; // firstIndex / firstVertex (we bind at offset 0)
                    wgpuQueueWriteBuffer(ctx.queue, d.indirectBuffer, 0,
                                         initialArgs, sizeof(initialArgs));

                    // Render bind group (set=2): {instance, culled} as read-only storage.
                    WGPUBindGroupEntry instBgEntries[2] = {};
                    instBgEntries[0].binding = 0;
                    instBgEntries[0].buffer = d.instanceBuffer;
                    instBgEntries[0].size = instanceBytes;
                    instBgEntries[1].binding = 1;
                    instBgEntries[1].buffer = d.culledBuffer;
                    instBgEntries[1].size = culledBytes;

                    WGPUBindGroupDescriptor bgDesc{};
                    bgDesc.label = {"bc_inst_bg", WGPU_STRLEN};
                    bgDesc.layout = m_instanceBindGroupLayout;
                    bgDesc.entryCount = 2;
                    bgDesc.entries = instBgEntries;
                    d.instanceBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &bgDesc);
                    if (!d.instanceBindGroup)
                        return false;

                    // Cull bind group (set=1): {instance ro, culled rw, indirect rw}.
                    WGPUBindGroupEntry cullBgEntries[3] = {};
                    cullBgEntries[0].binding = 0;
                    cullBgEntries[0].buffer = d.instanceBuffer;
                    cullBgEntries[0].size = instanceBytes;
                    cullBgEntries[1].binding = 1;
                    cullBgEntries[1].buffer = d.culledBuffer;
                    cullBgEntries[1].size = culledBytes;
                    cullBgEntries[2].binding = 2;
                    cullBgEntries[2].buffer = d.indirectBuffer;
                    cullBgEntries[2].size = kIndirectArgsSize;

                    WGPUBindGroupDescriptor cullBgDesc{};
                    cullBgDesc.label = {"bc_cull_bg", WGPU_STRLEN};
                    cullBgDesc.layout = m_cullBindGroupLayout;
                    cullBgDesc.entryCount = 3;
                    cullBgDesc.entries = cullBgEntries;
                    d.cullBindGroup = wgpuDeviceCreateBindGroup(ctx.device, &cullBgDesc);
                    if (!d.cullBindGroup)
                        return false;
                }
            }
            return true;
        }

        void CullInstances(pwgpu::test::SampleFrame &frame)
        {
            // Reset instanceCount to 0 for every drawable.
            for (Drawable &d : m_drawables)
            {
                wgpuCommandEncoderClearBuffer(frame.encoder, d.indirectBuffer, 4, 4);
            }

            WGPUComputePassDescriptor desc{};
            desc.label = {"bc_cull_pass", WGPU_STRLEN};
            WGPUComputePassEncoder pass =
                wgpuCommandEncoderBeginComputePass(frame.encoder, &desc);
            wgpuComputePassEncoderSetPipeline(pass, m_cullPipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, m_frameBindGroup, 0, nullptr);
            const uint32_t groups =
                (kMaxInstancesPerDrawable + kCullingWorkgroupSize - 1) / kCullingWorkgroupSize;
            for (const Drawable &d : m_drawables)
            {
                wgpuComputePassEncoderSetBindGroup(pass, 1, d.cullBindGroup, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(pass, groups, 1, 1);
            }
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
        }

        bool BuildBundles(pwgpu::test::SampleContext &ctx)
        {
            m_bundlePerspective = EncodeBundle(ctx, m_frameBindGroup, "bc_bundle_pers");
            m_bundleOverhead = EncodeBundle(ctx, m_overheadFrameBindGroup, "bc_bundle_over");
            return m_bundlePerspective && m_bundleOverhead;
        }

        WGPURenderBundle EncodeBundle(pwgpu::test::SampleContext &ctx,
                                      WGPUBindGroup frameBg, const char *label)
        {
            WGPUTextureFormat colorFormat = ctx.surfaceFormat;
            WGPURenderBundleEncoderDescriptor rbeDesc{};
            rbeDesc.label = {label, WGPU_STRLEN};
            rbeDesc.colorFormatCount = 1;
            rbeDesc.colorFormats = &colorFormat;
            rbeDesc.depthStencilFormat = WGPUTextureFormat_Depth24Plus;
            rbeDesc.sampleCount = kSampleCount;
            rbeDesc.depthReadOnly = WGPU_FALSE;
            rbeDesc.stencilReadOnly = WGPU_TRUE;

            WGPURenderBundleEncoder rbe =
                wgpuDeviceCreateRenderBundleEncoder(ctx.device, &rbeDesc);
            if (!rbe)
                return nullptr;

            wgpuRenderBundleEncoderSetPipeline(rbe, m_pipelineCulled);
            wgpuRenderBundleEncoderSetBindGroup(rbe, 0, frameBg, 0, nullptr);

            for (const Drawable &d : m_drawables)
            {
                wgpuRenderBundleEncoderSetBindGroup(
                    rbe, 1, m_materialBindGroups[d.materialIndex], 0, nullptr);
                wgpuRenderBundleEncoderSetBindGroup(rbe, 2, d.instanceBindGroup, 0, nullptr);
                wgpuRenderBundleEncoderSetVertexBuffer(
                    rbe, 0, d.mesh->vertexBuffer, 0, d.mesh->vertexBufferSize);
                if (d.mesh->hasIndices)
                {
                    wgpuRenderBundleEncoderSetIndexBuffer(
                        rbe, d.mesh->indexBuffer, WGPUIndexFormat_Uint16,
                        0, d.mesh->indexBufferSize);
                    wgpuRenderBundleEncoderDrawIndexedIndirect(rbe, d.indirectBuffer, 0);
                }
                else
                {
                    wgpuRenderBundleEncoderDrawIndirect(rbe, d.indirectBuffer, 0);
                }
            }

            WGPURenderBundleDescriptor bundleDesc{};
            bundleDesc.label = {label, WGPU_STRLEN};
            WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(rbe, &bundleDesc);
            wgpuRenderBundleEncoderRelease(rbe);
            return bundle;
        }

        void WriteFrameUniform(pwgpu::test::SampleContext &ctx,
                               WGPUBuffer buf,
                               const Mat4 &projection,
                               const Mat4 &view,
                               const float cameraPosition[3])
        {
            // Match tiny-webgpu-demo FRAME_BUFFER_SIZE = 64 f32 = 256 bytes.
            // Layout: proj (16) | view (16) | position (3) | time (1) |
            //         zRange (2) | _pad (2) | frustum[6*4] (24) | tail (?)
            //         = 16+16+3+1+2+2+24 = 64 f32. ✔
            float data[64] = {};
            std::memcpy(&data[0], projection.m, sizeof(float) * 16);
            std::memcpy(&data[16], view.m, sizeof(float) * 16);
            data[32] = cameraPosition[0];
            data[33] = cameraPosition[1];
            data[34] = cameraPosition[2];
            data[35] = static_cast<float>(ctx.timing.elapsedSeconds);
            data[36] = kZNear;
            data[37] = kZFar;
            data[38] = 0.0f;
            data[39] = 0.0f;

            float frustum[6 * 4] = {};
            BuildFrustum(projection, view, frustum);
            std::memcpy(&data[40], frustum, sizeof(frustum));

            wgpuQueueWriteBuffer(ctx.queue, buf, 0, data, sizeof(data));
        }

        void ReleaseRenderTargets()
        {
            if (m_msaaColorView)
            {
                wgpuTextureViewRelease(m_msaaColorView);
                m_msaaColorView = nullptr;
            }
            if (m_msaaColorTexture)
            {
                wgpuTextureRelease(m_msaaColorTexture);
                m_msaaColorTexture = nullptr;
            }
            if (m_depthView)
            {
                wgpuTextureViewRelease(m_depthView);
                m_depthView = nullptr;
            }
            if (m_depthTexture)
            {
                wgpuTextureRelease(m_depthTexture);
                m_depthTexture = nullptr;
            }
        }

        // --- shader modules
        WGPUShaderModule m_vsNormal = nullptr;
        WGPUShaderModule m_vsCulled = nullptr;
        WGPUShaderModule m_ps = nullptr;
        WGPUShaderModule m_cullCs = nullptr;

        // --- frame uniforms
        WGPUBuffer m_frameUniformBuffer = nullptr;
        WGPUBuffer m_overheadFrameUniformBuffer = nullptr;
        WGPUBindGroupLayout m_frameBindGroupLayout = nullptr;
        WGPUBindGroup m_frameBindGroup = nullptr;
        WGPUBindGroup m_overheadFrameBindGroup = nullptr;

        // --- material
        WGPUBindGroupLayout m_materialBindGroupLayout = nullptr;
        std::vector<WGPUBuffer> m_materialBuffers;
        std::vector<WGPUBindGroup> m_materialBindGroups;

        // --- geometry
        std::vector<GpuMesh> m_meshes;

        // --- bind group layouts / pipelines
        WGPUBindGroupLayout m_instanceBindGroupLayout = nullptr;
        WGPUBindGroupLayout m_cullBindGroupLayout = nullptr;
        WGPUPipelineLayout m_renderPipelineLayout = nullptr;
        WGPUPipelineLayout m_cullPipelineLayout = nullptr;
        WGPURenderPipeline m_pipelineNaive = nullptr;
        WGPURenderPipeline m_pipelineCulled = nullptr;
        WGPUComputePipeline m_cullPipeline = nullptr;

        // --- drawables
        std::vector<Drawable> m_drawables;

        // --- render bundles (one per camera view)
        WGPURenderBundle m_bundlePerspective = nullptr;
        WGPURenderBundle m_bundleOverhead = nullptr;

        // --- render targets
        WGPUTexture m_msaaColorTexture = nullptr;
        WGPUTextureView m_msaaColorView = nullptr;
        WGPUTexture m_depthTexture = nullptr;
        WGPUTextureView m_depthView = nullptr;

        // --- camera state (updated every frame)
        Mat4 m_projection = Mat4Identity();
        Mat4 m_view = Mat4Identity();
        Mat4 m_overheadView = Mat4Identity();
        float m_cameraPosition[3] = {0.0f, 0.0f, kCameraDistance};
        float m_orbitX = 0.0f;
        float m_orbitY = 0.0f;
        float m_cameraDistance = kCameraDistance;
        int m_lastMouseX = 0;
        int m_lastMouseY = 0;
        bool m_orbiting = false;
    };

    pwgpu::test::SampleAppDesc MakeBundleCullingDesc()
    {
        pwgpu::test::SampleAppDesc desc{};
        desc.sampleName = "BundleCullingTest";
        desc.windowTitle = "PhasmaWebGPU Bundle Culling";
        desc.initialWidth = kWidth;
        desc.initialHeight = kHeight;
        return desc;
    }
} // namespace

int main(int argc, char *argv[])
{
    BundleCullingSample sample;
    pwgpu::test::SampleApp app(sample, MakeBundleCullingDesc());
    return app.Run(argc, argv);
}
