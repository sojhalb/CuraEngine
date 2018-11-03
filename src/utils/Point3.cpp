//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "Point3.h" //The headers we're implementing.

namespace cura
{
// duct tape until the circular reference in CylPoint3 actually works
CylPoint3* Point3::toCylPoint3()
{
    coord_t theta = atan2(y, x);
    coord_t r = sqrt(pow(x,2) + pow(y,2));
    CylPoint3* cp = new CylPoint3(theta,y, r);
    return cp;
    // x = theta;
    // y = r;
}

// Point3* CylPoint3::toPoint3()
//     {
//         coord_t x = r * cos(theta);
//         coord_t z = r * sin(theta);
//         Point3 *p = new Point3(x, y, z);
//         return *p;
//     }

Point3 Point3::operator +(const Point3& p) const
{
    return Point3(x + p.x, y + p.y, z + p.z);
}

Point3 Point3::operator -(const Point3& p) const
{
    return Point3(x - p.x, y - p.y, z - p.z);
}

Point3 Point3::operator *(const Point3& p) const
{
    return Point3(x * p.x, y * p.y, z * p.z);
}

Point3 Point3::operator /(const Point3& p) const
{
    return Point3(x / p.x, y / p.y, z / p.z);
}

Point3& Point3::operator +=(const Point3& p)
{
    x += p.x;
    y += p.y;
    z += p.z;
    return *this;
}

Point3& Point3::operator -=(const Point3& p)
{
    x -= p.x;
    y -= p.y;
    z -= p.z;
    return *this;
}

Point3& Point3::operator *=(const Point3& p)
{
    x *= p.x;
    y *= p.y;
    z *= p.z;
    return *this;
}

Point3& Point3::operator /=(const Point3& p)
{
    x /= p.x;
    y /= p.y;
    z /= p.z;
    return *this;
}

bool Point3::operator ==(const Point3& p) const
{
    return x == p.x && y == p.y && z == p.z;
}

bool Point3::operator !=(const Point3& p) const
{
    return x != p.x || y != p.y || z != p.z;
}

}
