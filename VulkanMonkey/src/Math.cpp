#include "../include/Math.h"
#include <assert.h>

namespace vm {
	vec2::vec2()
	{
		x = 0.f;
		y = 0.f;
	}
	vec2::vec2(const float value)
	{
		x = y = value;
	}
	vec2::vec2(const float x, const float y)
	{
		this->x = x;
		this->y = y;
	}

	vec2::vec2(const vec2 & v)
	{
		x = v.x;
		y = v.y;
	}

	vec2::vec2(const float * v)
	{
		x = v[0];
		y = v[1];
	}

	vec2::vec2(const vec2 * v)
	{
		x = v->x;
		y = v->y;
	}

	void vec2::operator=(const vec2 & v)
	{
		x = v.x;
		y = v.y;
	}

	vec2 vec2::operator+(const vec2 & v) const
	{
		return vec2(x + v.x, y + v.y);
	}

	vec2 vec2::operator-() const
	{
		return vec2(-x, -y);
	}

	vec2 vec2::operator-(const vec2 & v) const
	{
		return vec2(x - v.x, y - v.y);
	}

	vec2 vec2::operator*(const vec2 & v) const
	{
		return vec2(x * v.x, y * v.y);
	}

	vec2 vec2::operator*(const float scalar) const
	{
		return vec2(x * scalar, y * scalar);
	}

	vec2 vec2::operator/(const vec2 & v) const
	{
		assert(v.x != 0.f && v.y != 0.f);
		return vec2(x / v.x, y / v.y);
	}

	vec2 vec2::operator/(const float scalar) const
	{
		assert(scalar != 0.f);
		return operator*(1.f/scalar);
	}

	void vec2::operator+=(const vec2 & v)
	{
		operator=(operator+(v));
	}

	void vec2::operator-=(const vec2 & v)
	{
		operator=(operator-(v));
	}

	void vec2::operator*=(const vec2 & v)
	{
		operator=(operator*(v));
	}

	void vec2::operator*=(const float scalar)
	{
		operator=(operator*(scalar));
	}

	void vec2::operator/=(const vec2 & v)
	{
		operator=(operator/(v));
	}

	void vec2::operator/=(const float scalar)
	{
		operator=(operator/(scalar));
	}

	float& vec2::operator[](unsigned i)
	{
		return (&x)[i];
	}

	float * vec2::ptr()
	{
		return &x;
	}

	vec3::vec3()
	{
		x = 0.f;
		y = 0.f;
		z = 0.f;
	}

	vec3::vec3(const float value)
	{
		x = y = z = value;
	}

	vec3::vec3(const float x, const float y, const float z)
	{
		this->x = x;
		this->y = y;
		this->z = z;
	}

	vec3::vec3(const vec2 & v, const float z)
	{
		x = v.x;
		y = v.y;
		this->z = z;
	}

	vec3::vec3(const vec3 & v)
	{
		x = v.x;
		y = v.y;
		z = v.z;
	}

	vec3::vec3(const float * v)
	{
		x = v[0];
		y = v[1];
		z = v[2];
	}

	vec3::vec3(const vec3 * v)
	{
		x = v->x;
		y = v->y;
		z = v->z;
	}

	void vec3::operator=(const vec3 & v)
	{
		x = v.x;
		y = v.y;
		z = v.z;
	}

	vec3 vec3::operator+(const vec3 & v) const
	{
		return vec3(x + v.x, y + v.y, z + v.z);
	}

	vec3 vec3::operator-() const
	{
		return vec3(-x, -y, -z);
	}

	vec3 vec3::operator-(const vec3 & v) const
	{
		return vec3(x - v.x, y - v.y, z - v.z);
	}

	vec3 vec3::operator*(const vec3 & v) const
	{
		return vec3(x * v.x, y * v.y, z * v.z);
	}

	vec3 vec3::operator*(const float scalar) const
	{
		return vec3(x * scalar, y * scalar, z * scalar);
	}

	vec3 vec3::operator/(const vec3 & v) const
	{
		assert(v.x != 0.f && v.y != 0.f && v.z != 0.f);
		return vec3(x / v.x, y / v.y,z / v.z);
	}

	vec3 vec3::operator/(const float scalar) const
	{
		assert(scalar != 0.f);
		return operator*(1.f/scalar);
	}

	void vec3::operator+=(const vec3 & v)
	{
		operator=(operator+(v));
	}

	void vec3::operator-=(const vec3 & v)
	{
		operator=(operator-(v));
	}

	void vec3::operator*=(const vec3 & v)
	{
		operator=(operator*(v));
	}

	void vec3::operator*=(const float scalar)
	{
		operator=(operator*(scalar));
	}

	void vec3::operator/=(const vec3 & v)
	{
		operator=(operator/(v));
	}

	void vec3::operator/=(const float scalar)
	{
		operator=(operator/(scalar));
	}

	float & vec3::operator[](unsigned i)
	{
		return (&x)[i];
	}

	float * vec3::ptr()
	{
		return &x;
	}

	vec4::vec4()
	{
		x = 0.f;
		y = 0.f;
		z = 0.f;
		w = 0.f;
	}

	vec4::vec4(const float value)
	{
		x = y = z = w = value;
	}

	vec4::vec4(const float x, const float y, const float z, const float w)
	{
		this->x = x;
		this->y = y;
		this->z = z;
		this->w = w;
	}

	vec4::vec4(const vec3 & v, const float w)
	{
		x = v.x;
		y = v.y;
		z = v.z;
		this->w = w;
	}

	vec4::vec4(const vec4 & v)
	{
		x = v.x;
		y = v.y;
		z = v.z;
		w = v.w;
	}

	vec4::vec4(const float * v)
	{
		x = v[0];
		y = v[1];
		z = v[2];
		w = v[3];
	}

	vec4::vec4(const vec4 * v)
	{
		x = v->x;
		y = v->y;
		z = v->z;
		w = v->w;
	}

	void vec4::operator=(const vec4 & v)
	{
		x = v.x;
		y = v.y;
		z = v.z;
		w = v.w;
	}

	vec4 vec4::operator+(const vec4 & v) const
	{
		return vec4(x + v.x, y + v.y, z + v.z, w + v.w);
	}

	vec4 vec4::operator-() const
	{
		return vec4(-x, -x, -z, -w);
	}

	vec4 vec4::operator-(const vec4 & v) const
	{
		return vec4(x - v.x, y - v.y, z - v.z, w - v.w);
	}

	vec4 vec4::operator*(const vec4 & v) const
	{
		return vec4(x * v.x, y * v.y,z * v.z, w * v.w);
	}

	vec4 vec4::operator*(const float scalar) const
	{
		return vec4(x * scalar,y * scalar, z * scalar, w * scalar);
	}

	vec4 vec4::operator/(const vec4 & v) const
	{
		assert(v.x != 0.f && v.y != 0.f && v.z != 0.f && v.w != 0.f);
		return vec4(x / v.x, y / v.y, z / v.z, w / v.w);
	}

	vec4 vec4::operator/(const float scalar) const
	{
		assert(scalar != 0.f);
		return operator*(1.f / scalar);
	}

	void vec4::operator+=(const vec4 & v)
	{
		operator=(operator+(v));
	}

	void vec4::operator-=(const vec4 & v)
	{
		operator=(operator-(v));
	}

	void vec4::operator*=(const vec4 & v)
	{
		operator=(operator*(v));
	}

	void vec4::operator*=(const float scalar)
	{
		operator=(operator*(scalar));
	}

	void vec4::operator/=(const vec4 & v)
	{
		operator=(operator/(v));
	}

	void vec4::operator/=(const float scalar)
	{
		operator=(operator/(scalar));
	}

	float & vec4::operator[](unsigned i)
	{
		return (&x)[i];
	}

	float * vec4::ptr()
	{
		return &x;
	}

	mat4::mat4()
	{
		_m[0] = { 0.f, 0.f, 0.f, 0.f };
		_m[1] = { 0.f, 0.f, 0.f, 0.f };
		_m[2] = { 0.f, 0.f, 0.f, 0.f };
		_m[3] = { 0.f, 0.f, 0.f, 0.f };
	}

	mat4::mat4(float diagonal)
	{
		_m[0] = { diagonal, 0.f, 0.f, 0.f };
		_m[1] = { 0.f, diagonal, 0.f, 0.f };
		_m[2] = { 0.f, 0.f, diagonal, 0.f };
		_m[3] = { 0.f, 0.f, 0.f, diagonal };
	}

	mat4::mat4(const float * m)
	{
		_m[0] = vec4(m[0],  m[1],  m[2],  m[3] );
		_m[1] = vec4(m[4],	m[5],  m[6],  m[7] );
		_m[2] = vec4(m[8],	m[9],  m[10], m[11]);
		_m[3] = vec4(m[12], m[13], m[14], m[15]);
	}

	mat4::mat4(const mat4 & m)
	{
		auto& mat = *const_cast<mat4*>(&m);
		_m[0] = mat[0];
		_m[1] = mat[1];
		_m[2] = mat[2];
		_m[3] = mat[3];
	}

	mat4::mat4(const mat4 * m)
	{
		auto& mat = *const_cast<mat4*>(m);
		_m[0] = mat[0];
		_m[1] = mat[1];
		_m[2] = mat[2];
		_m[3] = mat[3];
	}

	mat4::mat4(const vec4 & v0, const vec4 & v1, const vec4 & v2, const vec4 & v3)
	{
		_m[0] = v0;
		_m[1] = v1;
		_m[2] = v2;
		_m[3] = v3;
	}

	mat4::mat4
	(
		const float& x0, const float& y0, const float& z0, const float& w0,
		const float& x1, const float& y1, const float& z1, const float& w1,
		const float& x2, const float& y2, const float& z2, const float& w2,
		const float& x3, const float& y3, const float& z3, const float& w3
	)
	{
		_m[0] = vec4(x0, y0, z0, w0);
		_m[1] = vec4(x1, y1, z1, w1);
		_m[2] = vec4(x2, y2, z2, w2);
		_m[3] = vec4(x3, y3, z3, w3);
	}

	const mat4& mat4::identity()
	{
		static const mat4 idendity = mat4(1.f);
		return idendity;
	}

	void mat4::operator=(const mat4 & m)
	{
		auto& mat = *const_cast<mat4*>(&m);
		_m[0] = mat[0];
		_m[1] = mat[1];
		_m[2] = mat[2];
		_m[3] = mat[3];
	}

	mat4 mat4::operator*(const mat4 & m) const
	{
		mat4 mat = *const_cast<mat4*>(&m);
		return {
			_m[0] * mat[0][0] + _m[1] * mat[0][1] + _m[2] * mat[0][2] + _m[3] * mat[0][3],
			_m[0] * mat[1][0] + _m[1] * mat[1][1] + _m[2] * mat[1][2] + _m[3] * mat[1][3],
			_m[0] * mat[2][0] + _m[1] * mat[2][1] + _m[2] * mat[2][2] + _m[3] * mat[2][3],
			_m[0] * mat[3][0] + _m[1] * mat[3][1] + _m[2] * mat[3][2] + _m[3] * mat[3][3]
		};	
	}

	vec4 mat4::operator*(const vec4 & v) const
	{
		return
			_m[0] * vec4(v.x) +
			_m[1] * vec4(v.y) +
			_m[2] * vec4(v.z) +
			_m[3] * vec4(v.w);
	}

	mat4 mat4::operator*(const float scalar)
	{
		return mat4(_m[0] * scalar, _m[1] * scalar, _m[2] * scalar, _m[3] * scalar);
	}

	vec4 & mat4::operator[](unsigned i)
	{
		return _m[i];
	}

	float * mat4::ptr()
	{
		return &_m[0][0];
	}

	mat4 inverse(const mat4 & m)
	{
		auto& mat = *const_cast<mat4*>(&m);

		float Coef00 = mat[2][2] * mat[3][3] - mat[3][2] * mat[2][3];
		float Coef02 = mat[1][2] * mat[3][3] - mat[3][2] * mat[1][3];
		float Coef03 = mat[1][2] * mat[2][3] - mat[2][2] * mat[1][3];

		float Coef04 = mat[2][1] * mat[3][3] - mat[3][1] * mat[2][3];
		float Coef06 = mat[1][1] * mat[3][3] - mat[3][1] * mat[1][3];
		float Coef07 = mat[1][1] * mat[2][3] - mat[2][1] * mat[1][3];

		float Coef08 = mat[2][1] * mat[3][2] - mat[3][1] * mat[2][2];
		float Coef10 = mat[1][1] * mat[3][2] - mat[3][1] * mat[1][2];
		float Coef11 = mat[1][1] * mat[2][2] - mat[2][1] * mat[1][2];

		float Coef12 = mat[2][0] * mat[3][3] - mat[3][0] * mat[2][3];
		float Coef14 = mat[1][0] * mat[3][3] - mat[3][0] * mat[1][3];
		float Coef15 = mat[1][0] * mat[2][3] - mat[2][0] * mat[1][3];

		float Coef16 = mat[2][0] * mat[3][2] - mat[3][0] * mat[2][2];
		float Coef18 = mat[1][0] * mat[3][2] - mat[3][0] * mat[1][2];
		float Coef19 = mat[1][0] * mat[2][2] - mat[2][0] * mat[1][2];

		float Coef20 = mat[2][0] * mat[3][1] - mat[3][0] * mat[2][1];
		float Coef22 = mat[1][0] * mat[3][1] - mat[3][0] * mat[1][1];
		float Coef23 = mat[1][0] * mat[2][1] - mat[2][0] * mat[1][1];

		vec4 Fac0(Coef00, Coef00, Coef02, Coef03);
		vec4 Fac1(Coef04, Coef04, Coef06, Coef07);
		vec4 Fac2(Coef08, Coef08, Coef10, Coef11);
		vec4 Fac3(Coef12, Coef12, Coef14, Coef15);
		vec4 Fac4(Coef16, Coef16, Coef18, Coef19);
		vec4 Fac5(Coef20, Coef20, Coef22, Coef23);

		vec4 Vec0(mat[1][0], mat[0][0], mat[0][0], mat[0][0]);
		vec4 Vec1(mat[1][1], mat[0][1], mat[0][1], mat[0][1]);
		vec4 Vec2(mat[1][2], mat[0][2], mat[0][2], mat[0][2]);
		vec4 Vec3(mat[1][3], mat[0][3], mat[0][3], mat[0][3]);
		 
		vec4 Inv0(Vec1 * Fac0 - Vec2 * Fac1 + Vec3 * Fac2);
		vec4 Inv1(Vec0 * Fac0 - Vec2 * Fac3 + Vec3 * Fac4);
		vec4 Inv2(Vec0 * Fac1 - Vec1 * Fac3 + Vec3 * Fac5);
		vec4 Inv3(Vec0 * Fac2 - Vec1 * Fac4 + Vec2 * Fac5);
		 
		vec4 SignA(+1, -1, +1, -1);
		vec4 SignB(-1, +1, -1, +1);
		mat4 Inverse(Inv0 * SignA, Inv1 * SignB, Inv2 * SignA, Inv3 * SignB);

		vec4 Row0(Inverse[0][0], Inverse[1][0], Inverse[2][0], Inverse[3][0]);

		vec4 Dot0(mat[0] * Row0);
		float Dot1 = (Dot0.x + Dot0.y) + (Dot0.z + Dot0.w);

		float OneOverDeterminant = 1.f / Dot1;

		return Inverse * OneOverDeterminant;
	}

	mat4 transpose(const mat4& m)
	{
		auto& mat = *const_cast<mat4*>(&m);
		return {
			{ mat[0][0], mat[1][0], mat[2][0], mat[3][0] },
			{ mat[0][1], mat[1][1], mat[2][1], mat[3][1] },
			{ mat[0][2], mat[1][2], mat[2][2], mat[3][2] },
			{ mat[0][3], mat[1][3], mat[2][3], mat[3][3] }
		};
	}

	mat4 translate(const mat4& m, const vec3& v)
	{
		mat4 mat(m);
		mat[3] = mat[0] * v.x + mat[1] * v.y + mat[2] * v.z + mat[3];
		return mat;
	}

	mat4 scale(const mat4 & m, const vec3 & v)
	{
		mat4 mat(1.f);
		mat[0][0] = v.x;
		mat[1][1] = v.y;
		mat[2][2] = v.z;
		return m * mat;
	}

	mat4 rotate(const mat4 & m, float angle, const vec3 & v)
	{
		const float c = cos(angle);
		const float s = sin(angle);

		vec3 axis = normalize(v);

		mat4 mat;
		mat[0][0] = c + (1.f - c) * axis.x * axis.x;
		mat[0][1] = (1.f - c) * axis.x * axis.y + s * axis.z;
		mat[0][2] = (1.f - c) * axis.x * axis.z - s * axis.y;
		mat[0][3] = 0.f;

		mat[1][0] = (1.f - c) * axis.y * axis.x - s * axis.z;
		mat[1][1] = c + (1.f - c) * axis.y * axis.y;
		mat[1][2] = (1.f - c) * axis.y * axis.z + s * axis.x;
		mat[1][3] = 0.f;

		mat[2][0] = (1.f - c) * axis.z * axis.x + s * axis.y;
		mat[2][1] = (1.f - c) * axis.z * axis.y - s * axis.x;
		mat[2][2] = c + (1.f - c) * axis.z * axis.z;
		mat[2][3] = 0.f;

		mat[3] = vec4(0.f, 0.f, 0.f, 1.f);
		return m * mat;
	}

	mat4 perspective(const float fovy, const float aspect, const float zNear, const float zFar)
	{
		mat4 mat;

		float const tanHalfFovy = tan(fovy / 2.f);
		mat[0][0] = 1.f / (aspect * tanHalfFovy);
		mat[1][1] = 1.f / (tanHalfFovy);
		mat[2][3] = 1.f;
		mat[2][2] = zFar / (zFar - zNear);
		mat[3][2] = -(zFar * zNear) / (zFar - zNear);

		return mat;
	}

	mat4 ortho(const float left, const float right, const float bottom, const float top, const float zNear, const float zFar)
	{
		mat4 mat(1.f);
		mat[0][0] = 2.f / (right - left);
		mat[1][1] = 2.f / (top - bottom);
		mat[3][0] = -(right + left) / (right - left);
		mat[3][1] = -(top + bottom) / (top - bottom);

		mat[2][2] = 1.f / (zFar - zNear);
		mat[3][2] = -zNear / (zFar - zNear);

		return mat;
	}

	mat4 lookAt(const vm::vec3& eye, const vm::vec3& center, const vm::vec3& up)
	{
		vm::vec3 _front(normalize(center - eye));
		vm::vec3 _right(normalize(cross(up, _front)));
		vm::vec3 _up(cross(_front, _right));

		mat4 mat(1.f);
		mat[0][0] = _right.x;
		mat[1][0] = _right.y;
		mat[2][0] = _right.z;
		mat[0][1] = _up.x;
		mat[1][1] = _up.y;
		mat[2][1] = _up.z;
		mat[0][2] = _front.x;
		mat[1][2] = _front.y;
		mat[2][2] = _front.z;
		mat[3][0] = -dot(_right, eye);
		mat[3][1] = -dot(_up, eye);
		mat[3][2] = -dot(_front, eye);
		return mat;
	}

	float length(const vec2 & v)
	{
		return sqrt(dot(v, v));
	}

	float length(const vec3 & v)
	{
		return sqrt(dot(v, v));
	}

	float length(const vec4 & v)
	{
		return sqrt(dot(v, v));
	}

	vec2 normalize(const vec2 & v)
	{
		return v * inversesqrt(dot(v, v));
	}

	vec3 normalize(const vec3 & v)
	{
		return v * inversesqrt(dot(v, v));
	}

	vec4 normalize(const vec4 & v)
	{
		return v * inversesqrt(dot(v, v));
	}

	float dot(const vec2 & v1, const vec2 & v2)
	{
		vec2 vec(v1 * v2);
		return vec.x + vec.y;
	}

	float dot(const vec3 & v1, const vec3 & v2)
	{
		vec3 vec(v1 * v2);
		return vec.x + vec.y + vec.z;
	}

	float dot(const vec4 & v1, const vec4 & v2)
	{
		vec4 vec(v1 * v2);
		return vec.x + vec.y + vec.z + vec.w;
	}

	vec3 cross(const vec3 & v1, const vec3 & v2)
	{
		return vec3(
			v1.y * v2.z - v2.y * v1.z,
			v1.z * v2.x - v2.z * v1.x,
			v1.x * v2.y - v2.x * v1.y);
	}

	float inversesqrt(float x)
	{
		return 1.f / sqrt(x);
	}

	float radians(float degrees)
	{
		 return degrees * 0.01745329251994329576923690768489f;
	}

	float degrees(float radians)
	{
		return radians * 57.295779513082320876798154814105f;
	}

}
