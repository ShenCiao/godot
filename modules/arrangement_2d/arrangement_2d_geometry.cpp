#include "arrangement_2d.h"

#include <cmath>

bool Arrangement2D::PointLess::operator()(const CGAL::Point &p_a, const CGAL::Point &p_b) const {
	CGAL::Segment_traits traits;
	CGAL::Segment_traits::Compare_xy_2 compare_xy = traits.compare_xy_2_object();
	return compare_xy(p_a, p_b) == CGAL::SMALLER;
}

bool Arrangement2D::points_equal(const CGAL::Point &p_a, const CGAL::Point &p_b) {
	CGAL::Segment_traits traits;
	CGAL::Segment_traits::Compare_xy_2 compare_xy = traits.compare_xy_2_object();
	return compare_xy(p_a, p_b) == CGAL::EQUAL;
}

bool Arrangement2D::point_on_segment(const CGAL::Point &p_from, const CGAL::Point &p_point, const CGAL::Point &p_to) {
	CGAL::Segment_traits traits;
	CGAL::Segment_traits::Collinear_2 collinear = traits.collinear_2_object();
	if (!collinear(p_from, p_point, p_to)) {
		return false;
	}
	CGAL::Segment_traits::Collinear_are_ordered_along_line_2 ordered = traits.collinear_are_ordered_along_line_2_object();
	return ordered(p_from, p_point, p_to);
}

double Arrangement2D::segment_fraction(const CGAL::Point &p_from, const CGAL::Point &p_to, const CGAL::Point &p_point) {
	double dx = CGAL::to_double(p_to.x() - p_from.x());
	double dy = CGAL::to_double(p_to.y() - p_from.y());
	double numerator = std::abs(dx) >= std::abs(dy)
			? CGAL::to_double(p_point.x() - p_from.x())
			: CGAL::to_double(p_point.y() - p_from.y());
	double denominator = std::abs(dx) >= std::abs(dy) ? dx : dy;
	if (denominator == 0.0) {
		return 0.0;
	}
	return numerator / denominator;
}

int64_t Arrangement2D::quantize_t(float p_t) {
	return static_cast<int64_t>(std::llround(static_cast<double>(p_t) / 0.0001));
}
