/** Copyright (C) 2016 Ultimaker - Released under terms of the AGPLv3 License */
#ifndef PATH_PLANNING_NOZZLE_CUT_INSERT_H
#define PATH_PLANNING_NOZZLE_CUT_INSERT_H

#include "../gcodeExport.h"

namespace cura 
{

/*!
 * A gcode command to insert before a specific path.
 * 
 * Currently only used for preheat commands
 */
struct CutInsert
{
    const unsigned int path_idx; //!< The path before which to insert this command
    const unsigned int points_idx; //!< The point before which to insert this command
    int extruder;
    CutInsert(unsigned int path_idx, unsigned int points_idx, int extruder);

    /*!
     * Write the temperature command at the current position in the gcode.
     * \param gcode The actual gcode writer
     */
    void write(GCodeExport& gcode);
};
}//namespace cura

#endif//PATH_PLANNING_NOZZLE_TEMP_INSERT_H
