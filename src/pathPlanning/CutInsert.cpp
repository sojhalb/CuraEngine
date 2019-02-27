//Copyright (C) 2016 Ultimaker
//Released under terms of the AGPLv3 License

#include "CutInsert.h"

namespace cura
{

CutInsert::CutInsert(unsigned int path_idx, unsigned int points_idx, int extruder)
: path_idx(path_idx)
, points_idx(points_idx)
, extruder(extruder)
{
    //assert(temperature != 0 && temperature != -1 && "Temperature command must be set!");
}

void CutInsert::write(GCodeExport& gcode)
{
    gcode.writeCutCommand(extruder);
}

}