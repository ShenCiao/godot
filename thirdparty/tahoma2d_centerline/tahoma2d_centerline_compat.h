#pragma once

// Small Toonz-compatible value layer used by the imported centerline stages.
// The algorithms remain in their original data-oriented form while the public
// module bridge converts the final thick points to Godot variants.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

struct TPoint {
	int x = 0;
	int y = 0;
	TPoint() = default;
	TPoint(int p_x, int p_y) : x(p_x), y(p_y) {}
	TPoint(double p_x, double p_y) : x(int(std::lround(p_x))), y(int(std::lround(p_y))) {}
	TPoint operator+(const TPoint &p_other) const { return TPoint(x + p_other.x, y + p_other.y); }
	TPoint operator-(const TPoint &p_other) const { return TPoint(x - p_other.x, y - p_other.y); }
	TPoint operator*(int p_scalar) const { return TPoint(x * p_scalar, y * p_scalar); }
	bool operator==(const TPoint &p_other) const { return x == p_other.x && y == p_other.y; }
};

inline int cross(const TPoint &p_a, const TPoint &p_b) {
	return p_a.x * p_b.y - p_a.y * p_b.x;
}

struct TPointD {
	double x = 0.0;
	double y = 0.0;
	TPointD() = default;
	TPointD(double p_x, double p_y) : x(p_x), y(p_y) {}
	TPointD(const TPoint &p_point) : x(p_point.x), y(p_point.y) {}
	TPointD operator+(const TPointD &p_other) const { return TPointD(x + p_other.x, y + p_other.y); }
	TPointD operator-(const TPointD &p_other) const { return TPointD(x - p_other.x, y - p_other.y); }
	TPointD operator-() const { return TPointD(-x, -y); }
	TPointD operator*(double p_scalar) const { return TPointD(x * p_scalar, y * p_scalar); }
	double operator*(const TPointD &p_other) const { return x * p_other.x + y * p_other.y; }
	TPointD operator/(double p_scalar) const { return TPointD(x / p_scalar, y / p_scalar); }
	TPointD &operator+=(const TPointD &p_other) { x += p_other.x; y += p_other.y; return *this; }
	TPointD &operator-=(const TPointD &p_other) { x -= p_other.x; y -= p_other.y; return *this; }
	TPointD &operator*=(double p_scalar) { x *= p_scalar; y *= p_scalar; return *this; }
	bool operator==(const TPointD &p_other) const { return x == p_other.x && y == p_other.y; }
};

inline TPointD operator*(double p_scalar, const TPointD &p_point) { return p_point * p_scalar; }
inline double cross(const TPointD &p_a, const TPointD &p_b) { return p_a.x * p_b.y - p_a.y * p_b.x; }
inline double norm(const TPointD &p_point) { return std::sqrt(p_point.x * p_point.x + p_point.y * p_point.y); }
inline double norm2(const TPointD &p_point) { return p_point * p_point; }
inline TPointD normalize(const TPointD &p_point) {
	const double length = norm(p_point);
	return length > 0.0 ? p_point / length : TPointD();
}
inline TPointD convert(const TPoint &p_point) { return TPointD(p_point); }
inline TPoint convert(const TPointD &p_point) { return TPoint(p_point.x, p_point.y); }
inline TPointD rotate90(const TPointD &p_point) { return TPointD(-p_point.y, p_point.x); }
inline TPointD rotate270(const TPointD &p_point) { return TPointD(p_point.y, -p_point.x); }

struct T3DPointD {
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	T3DPointD() = default;
	T3DPointD(double p_x, double p_y, double p_z = 0.0) : x(p_x), y(p_y), z(p_z) {}
	T3DPointD(const TPointD &p_point, double p_z = 0.0) : x(p_point.x), y(p_point.y), z(p_z) {}
	T3DPointD operator+(const T3DPointD &p_other) const { return T3DPointD(x + p_other.x, y + p_other.y, z + p_other.z); }
	T3DPointD operator-(const T3DPointD &p_other) const { return T3DPointD(x - p_other.x, y - p_other.y, z - p_other.z); }
	T3DPointD operator-() const { return T3DPointD(-x, -y, -z); }
	T3DPointD operator*(double p_scalar) const { return T3DPointD(x * p_scalar, y * p_scalar, z * p_scalar); }
	T3DPointD operator/(double p_scalar) const { return T3DPointD(x / p_scalar, y / p_scalar, z / p_scalar); }
	T3DPointD &operator+=(const T3DPointD &p_other) { x += p_other.x; y += p_other.y; z += p_other.z; return *this; }
	T3DPointD &operator-=(const T3DPointD &p_other) { x -= p_other.x; y -= p_other.y; z -= p_other.z; return *this; }
	T3DPointD &operator*=(double p_scalar) { x *= p_scalar; y *= p_scalar; z *= p_scalar; return *this; }
	bool operator==(const T3DPointD &p_other) const { return x == p_other.x && y == p_other.y && z == p_other.z; }
	// Toonz uses * for the 3D dot product and scalar multiplication.
	double operator*(const T3DPointD &p_other) const { return x * p_other.x + y * p_other.y + z * p_other.z; }
};

inline T3DPointD operator*(double p_scalar, const T3DPointD &p_point) { return p_point * p_scalar; }
inline double norm(const T3DPointD &p_point) { return std::sqrt(p_point * p_point); }
inline double norm2(const T3DPointD &p_point) { return p_point * p_point; }
inline double tdistance(const T3DPointD &p_a, const T3DPointD &p_b) { return norm(p_a - p_b); }
inline T3DPointD normalize(const T3DPointD &p_point) {
	const double length = norm(p_point);
	return length > 0.0 ? p_point / length : T3DPointD();
}
inline T3DPointD cross(const T3DPointD &p_a, const T3DPointD &p_b) {
	return T3DPointD(p_a.y * p_b.z - p_a.z * p_b.y,
			p_a.z * p_b.x - p_a.x * p_b.z,
			p_a.x * p_b.y - p_a.y * p_b.x);
}

struct TAffine {
	double a11 = 1.0;
	double a12 = 0.0;
	double a13 = 0.0;
	double a21 = 0.0;
	double a22 = 1.0;
	double a23 = 0.0;
	TAffine() = default;
	TAffine(double p_a11, double p_a12, double p_a13, double p_a21, double p_a22, double p_a23) :
			a11(p_a11), a12(p_a12), a13(p_a13), a21(p_a21), a22(p_a22), a23(p_a23) {}
	double det() const { return a11 * a22 - a12 * a21; }
	TAffine inv() const {
		const double determinant = det();
		return TAffine(a22 / determinant, -a12 / determinant, (a12 * a23 - a22 * a13) / determinant,
				-a21 / determinant, a11 / determinant, (a21 * a13 - a11 * a23) / determinant);
	}
	TPointD operator*(const TPointD &p_point) const {
		return TPointD(a11 * p_point.x + a12 * p_point.y + a13,
				a21 * p_point.x + a22 * p_point.y + a23);
	}
};

struct TThickPoint {
	double x = 0.0;
	double y = 0.0;
	double thick = 0.0;
	TThickPoint() = default;
	TThickPoint(double p_x, double p_y, double p_thick) : x(p_x), y(p_y), thick(p_thick) {}
	TThickPoint(const T3DPointD &p_point) : x(p_point.x), y(p_point.y), thick(p_point.z) {}
	operator T3DPointD() const { return T3DPointD(x, y, thick); }
};

class TStroke {
	std::vector<TThickPoint> m_control_points;
	int m_flags = 0;

public:
	explicit TStroke(const std::vector<TThickPoint> &p_control_points) : m_control_points(p_control_points) {}
	int getControlPointCount() const { return int(m_control_points.size()); }
	TThickPoint getControlPoint(int p_index) const { return m_control_points[p_index]; }
	void setControlPoint(int p_index, const TThickPoint &p_point) { m_control_points[p_index] = p_point; }
	const std::vector<TThickPoint> &getControlPoints() const { return m_control_points; }
	void setFlag(int p_flag, bool p_enabled) { m_flags = p_enabled ? (m_flags | p_flag) : (m_flags & ~p_flag); }
	bool getFlag(int p_flag) const { return (m_flags & p_flag) != 0; }
};

struct CompatPixel {
	uint8_t r = 255;
	uint8_t g = 255;
	uint8_t b = 255;
	uint8_t m = 255;
	uint8_t value = 255;
	uint8_t tone = 255;
	int ink = 0;
	int getTone() const { return tone; }
	int getInk() const { return ink; }
};

struct CompatRasterBounds {
	int width = 0;
	int height = 0;
	bool contains(const TPoint &p_point) const {
		return p_point.x >= 0 && p_point.y >= 0 && p_point.x < width && p_point.y < height;
	}
};

class CompatRaster {
	int m_width = 0;
	int m_height = 0;
	std::vector<CompatPixel> m_pixels;

public:
	CompatRaster() = default;
	CompatRaster(int p_width, int p_height) : m_width(p_width), m_height(p_height), m_pixels(size_t(p_width) * size_t(p_height)) {}
	int getLx() const { return m_width; }
	int getLy() const { return m_height; }
	CompatRasterBounds getBounds() const { return { m_width, m_height }; }
	void lock() {}
	void unlock() {}
	CompatPixel *pixels(int p_y) { return m_pixels.data() + size_t(p_y) * size_t(m_width); }
	const CompatPixel *pixels(int p_y) const { return m_pixels.data() + size_t(p_y) * size_t(m_width); }
};

using TPixel32 = CompatPixel;
using TPixelGR8 = CompatPixel;
using TPixelCM32 = CompatPixel;
using TRasterP = CompatRaster *;
using TRaster32P = CompatRaster *;
using TRasterGR8P = CompatRaster *;
using TRasterCM32P = CompatRaster *;
template <typename T> using TRasterPT = CompatRaster *;

struct TPalette {};

struct CenterlineConfiguration {
	int m_threshold = 200;
	int m_despeckling = 10;
	double m_maxThickness = 100.0;
	double m_penalty = 0.5;
	double m_thicknessRatio = 100.0;
};

class VectorizerCore {
	bool m_canceled = false;

public:
	bool isCanceled() const { return m_canceled; }
	void setOverallPartials(int) {}
	void emitPartialDone() {}
};

inline int sq(int p_value) { return p_value * p_value; }
