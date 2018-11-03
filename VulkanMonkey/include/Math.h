#pragma once
#include <cmath>

// Left Handed, Colomn Major, Depth[0,1]
namespace vm
{
	struct vec2 {
		vec2();
		vec2(const float value);
		vec2(const float x, const float y);
		vec2(const vec2& v);
		vec2(const float* v);
		vec2(const vec2* v);
		void operator=(const vec2& v);
		vec2 operator+(const vec2& v) const;
		vec2 operator-() const;
		vec2 operator-(const vec2& v) const;
		vec2 operator*(const vec2& v) const;
		vec2 operator*(const float scalar) const;
		vec2 operator/(const vec2& v) const;
		vec2 operator/(const float scalar) const;
		void operator+=(const vec2& v);
		void operator-=(const vec2& v);
		void operator*=(const vec2& v);
		void operator*=(const float scalar);
		void operator/=(const vec2& v);
		void operator/=(const float scalar);
		float& operator[](const unsigned i);
		float* ptr();

		float x, y;
	};

	struct vec3 {
		vec3();
		vec3(const float value);
		vec3(const float x, const float y, const float z);
		vec3(const vec2& v, const float z);
		vec3(const vec3& v);
		vec3(const float* v);
		vec3(const vec3* v);
		void operator=(const vec3& v);
		vec3 operator+(const vec3& v) const;
		vec3 operator-() const;
		vec3 operator-(const vec3& v) const;
		vec3 operator*(const vec3& v) const;
		vec3 operator*(const float scalar) const;
		vec3 operator/(const vec3& v) const;
		vec3 operator/(const float scalar) const;
		void operator+=(const vec3& v);
		void operator-=(const vec3& v);
		void operator*=(const vec3& v);
		void operator*=(const float scalar);
		void operator/=(const vec3& v);
		void operator/=(const float scalar);
		float& operator[](const unsigned i);
		float* ptr();

		float x, y, z;
	};

	struct vec4 {
		vec4();
		vec4(const float value);
		vec4(const float x, const float y, const float z, const float w);
		vec4(const vec3& v, const float w);
		vec4(const vec4& v);
		vec4(const float* v);
		vec4(const vec4* v);
		void operator=(const vec4& v);
		vec4 operator+(const vec4& v) const;
		vec4 operator-() const;
		vec4 operator-(const vec4& v) const;
		vec4 operator*(const vec4& v) const;
		vec4 operator*(const float scalar) const;
		vec4 operator/(const vec4& v) const;
		vec4 operator/(const float scalar) const;
		void operator+=(const vec4& v);
		void operator-=(const vec4& v);
		void operator*=(const vec4& v);
		void operator*=(const float scalar);
		void operator/=(const vec4& v);
		void operator/=(const float scalar);
		float& operator[](const unsigned i);
		float* ptr();

		float x, y, z, w;
	};

	struct mat4 {
		mat4();
		mat4(const float diagonal);
		mat4(const float* m);
		mat4(const mat4* m);
		mat4(const mat4& m);
		mat4(const vec4& v0, const vec4& v1, const vec4& v2, const vec4& v3);
		mat4(const float& x0, const float& y0, const float& z0, const float& w0,
			const float& x1, const float& y1, const float& z1, const float& w1,
			const float& x2, const float& y2, const float& z2, const float& w2,
			const float& x3, const float& y3, const float& z3, const float& w3);
		static const mat4& identity();
		void operator=(const mat4& m);
		mat4 operator*(const mat4& m) const;
		vec4 operator*(const vec4& m) const;
		mat4 operator*(const float scalar);
		vec4& operator[](const unsigned i);
		float* ptr();

		vec4 _m[4];
	};

	mat4 inverse(const mat4& m);
	mat4 transpose(const mat4& m);
	mat4 translate(const mat4& m, const vec3& v);
	mat4 scale(const mat4& m, const vec3& v);
	mat4 rotate(const mat4& m, const float angle, const vec3& v);
	mat4 perspective(const float fovy, const float aspect, const float zNear, const float zFar);
	mat4 ortho(const float left, const float right, const float bottom, const float top, const float zNear, const float zFar);
	mat4 lookAt(const vm::vec3& eye, const vm::vec3& center, const vm::vec3& up);
	float length(const vec2& v);
	float length(const vec3& v);
	float length(const vec4& v);
	float dot(const vec2& v1, const vec2& v2);
	float dot(const vec3& v1, const vec3& v2);
	float dot(const vec4& v1, const vec4& v2);
	vec2 normalize(const vec2& v);
	vec3 normalize(const vec3& v);
	vec4 normalize(const vec4& v);
	vec3 cross(const vec3& v1, const vec3& v2);
	float inversesqrt(const float x);
	float radians(const float degrees);
	float degrees(const float radians);
}