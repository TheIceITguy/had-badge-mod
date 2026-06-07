"""Host tests for great-circle distance/bearing."""
from services.geo import distance_m, bearing_deg


def test_one_degree_longitude_at_equator():
    assert abs(distance_m(0, 0, 0, 1) - 111195) < 500


def test_same_point_is_zero():
    assert distance_m(45.8, 9.4, 45.8, 9.4) < 1e-6


def test_bearing_north_and_east():
    assert abs(bearing_deg(0, 0, 1, 0) - 0) < 1e-6
    assert abs(bearing_deg(0, 0, 0, 1) - 90) < 1e-6
