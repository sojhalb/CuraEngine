//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.
#include <stdio.h>

#include <algorithm> // remove_if

#include "utils/gettime.h"
#include "utils/logoutput.h"
#include "utils/SparsePointGridInclusive.h"
#include "utils/CylSolver.h"

#include "slicer.h"

namespace cura
{

int largest_neglected_gap_first_phase = MM2INT(0.01);  //!< distance between two line segments regarded as connected
int largest_neglected_gap_second_phase = MM2INT(0.02); //!< distance between two line segments regarded as connected
int max_stitch1 = MM2INT(10.0);                        //!< maximal distance stitched between open polylines to form polygons

void SlicerLayer::makeBasicPolygonLoops(Polygons &open_polylines, Polygon& open_digon, const Mesh* mesh, int layer_nr)
{
    for (unsigned int start_segment_idx = 0; start_segment_idx < segments.size(); start_segment_idx++)
    {
        if (!segments[start_segment_idx].addedToPolygon)
        {
            makeBasicPolygonLoop(open_polylines, open_digon, start_segment_idx, mesh, layer_nr);
        }
    }
    //Clear the segmentList to save memory, it is no longer needed after this point.
    segments.clear();
}

void SlicerLayer::makeBasicPolygonLoop(Polygons &open_polylines, Polygon &open_digon, unsigned int start_segment_idx, const Mesh* mesh, int layer_nr)
{
    Polygon poly;
     // used to store the first edge of a digon while the 2nd is found
    poly.add(segments[start_segment_idx].start);

    float winding_radians = 0;
    for (int segment_idx = start_segment_idx; segment_idx != -1;)
    {
        SlicerSegment &segment = segments[segment_idx];
        poly.add(segment.end);
        // segment goes over the LHS sign change
        coord_t seg_start = segment.start.X;
        coord_t seg_end = segment.end.X;
        if (segment.start.X > (PT5PI * THETAFACTOR) && segment.end.X < 0)
        {
            seg_end += 2*PI * THETAFACTOR;
        }
        else if (segment.end.X > (PT5PI * THETAFACTOR) && segment.start.X < 0)
        {
            seg_start += 2*PI *THETAFACTOR;
        }

        float arc = seg_end - seg_start;
        if (abs(abs(arc) - ( PI * THETAFACTOR )) < 100 && abs(abs(arc) - ( PI * THETAFACTOR )) > 0) // just get kinda close to PI
        {
            arc = abs(arc); //sure
        }
        winding_radians += arc / THETAFACTOR; // good old milli theta
        segment.addedToPolygon = true;
        segment_idx = getNextSegmentIdx(segment, start_segment_idx);
        if (segment_idx == static_cast<int>(start_segment_idx))
        { // polyon is closed
            //assuming max winding of 1
            if (abs((abs(winding_radians)) - 2*PI) < 0.001 && abs(abs(winding_radians) - 2*PI) > 0) // a very generous tolerance but it should be pretty close to 1
            {
                // here is where you would make digons if you were making digons
                // instead we will be adding a seam and treating everything as a non-wrapping polygon
                if (open_digon.empty())
                {
                    //no open digon, start one
                    *open_digon = *poly;
                    winding_radians = 0;
                }
                else 
                {
                    // otherwise generate a seam and add the combined flattened polygon
                    // hopefully first ring is CCW wrt the cyl_axis and 2nd ring is CW
                    // just use the start of the first line seg of the first ring
                    if((*open_digon).size() == 0)
                    {
                        assert(false);
                    }

                    // add 2 pi to the lower digon (y == 0), todo add a check
                    // int bottom_max_idx = 1;
                    // int top_min_idx = 1;
                    // auto bottom_max = *max_element(std::begin(open_digon), std::end(open_digon));

                    // assuming that open digon is always decreasing (Y is > than poly) and poly is increasing..

                    if(open_digon.front().Y < poly.front().Y)
                    {
                        auto temp = open_digon;
                        open_digon.clear();
                        for (int i = 0; i < poly.size(); i++)
                        {
                            open_digon.add(poly[i]);
                        }
                        poly.clear();
                        for (int j = 0; j < temp.size(); j++)
                        {
                            poly.add(temp[j]);
                        }
                    }

                    if (open_digon.front().X == open_digon.back().X)
                    {
                        open_digon.front().X += 2*PI*THETAFACTOR;
                    }

                    if (poly.front().X == poly.back().X)
                    {
                        poly.back().X += 2*PI*THETAFACTOR;
                    }
                    // auto bottom_max_iter = max_element(open_digon.begin(), open_digon.end());

                    auto top_max_iter = max_element(poly.begin(), poly.end());

                    // // add 2*PI to points in bottom ring
                    // bottom_max_iter++;
                    // for(; bottom_max_iter != open_digon.end(); bottom_max_iter++)
                    // {
                    //     *bottom_max_iter += 2*PI*THETAFACTOR;
                    // }

                    // // sub 2*PI to points in top ring
                    // for(auto iter = poly.begin(); iter != top_max_iter; iter++)
                    // {
                    //     *iter -= PI*THETAFACTOR;
                    // }
                    // // rotate the upper digon left by top_turnback
                    //std::rotate((*poly).begin(), (*poly).begin() + (top_max_iter - poly.begin()), (*poly).end());
                    // // sub 2 pi from the upper digon from the end

                    // so far seam is just constant
                    coord_t seam = mesh->getSettingInMillimeters("seam_front");
                    coord_t seam2 = mesh->getSettingInMillimeters("seam_back");
                    
                    if(layer_nr % 2) 
                    {
                        coord_t temp = seam2;
                        seam2 = seam;
                        seam = temp;
                    }
                    
                    Point* pt_bot =  new Point{seam, poly.front().Y};
                    Point* pt_top = new Point{seam2, open_digon.front().Y};

                    auto seam_bot = std::lower_bound(poly.begin(), poly.end(), seam);

                    (*poly).insert(seam_bot, 1, *pt_bot);

                    for (auto it = seam_bot + 1; it != poly.end(); ++it)
                    {
                        (*it).X -= (2*PI*THETAFACTOR);
                    }

                    ClipperLib::IntPoint test {seam, poly.front().Y};
                    auto ab = (*poly).insert(seam_bot, 1, test);
                    (++ab)->X -= 2*PI*THETAFACTOR;

                    ClipperLib::Path::reverse_iterator seam_top = std::lower_bound(open_digon.rbegin(), open_digon.rend(), seam);
                    //(*open_digon).insert(seam_top.base(), 1, *pt_top);
                    ClipperLib::Path::iterator temp = seam_top.base();
                    for (ClipperLib::Path::iterator it = open_digon.begin(); it != temp; ++it)
                    {
                        (*it).X -= (2*PI*THETAFACTOR);
                    }
                    std::rotate((*open_digon).begin(), (*open_digon).begin() + (seam_top.base() - open_digon.begin()), (*open_digon).end());
                    (*open_digon).insert((*open_digon).begin(),1, *pt_top);

                    ClipperLib::IntPoint test2 {seam2, open_digon.front().Y};
                    (*open_digon).emplace_back(test2);
                    (*open_digon).back().X -= 2*PI*THETAFACTOR;

                    ClipperLib::IntPoint start_seam_pt = (*poly).at(0);
                    //append the entire old digon to the new poly
                    seam_bot = std::find(poly.begin(), poly.end(), seam);
                    (*poly).insert(seam_bot + 1, open_digon.begin(), open_digon.end());
                    // add the start point to the end to close the poly
                    (*poly).push_back(start_seam_pt);

                    polygons.add(poly);
                    polygons.has_digon = true;
                }
            }
            else
            {
                polygons.add(poly);
            }
            return;
        }
    }
    // polygon couldn't be closed
    open_polylines.add(poly);
}

int SlicerLayer::tryFaceNextSegmentIdx(SlicerSegment &segment, int face_idx, unsigned int start_segment_idx)
{
    decltype(face_idx_to_segment_idx.begin()) it;
    auto range = face_idx_to_segment_idx.equal_range(face_idx);

    // log("for face: %d, found matching segments: ", face_idx);
    // for(auto it = range.first; it != range.second; ++it)
    // {
    //     log("%d ", it->second); 
    // }
    // log("\n");

    for(auto it = range.first; it != range.second; ++it)
    {
        int segment_idx = it->second;
        Point p1 = segments[segment_idx].start;
        Point diff = cylDiff(segment.end,p1);
        if (shorterThen(diff, largest_neglected_gap_first_phase))
        {
            // auto temp = (segment.end.X - p1.X);
            // auto temp2 = int(2*PI*THETAFACTOR);
            int wrapping_dir = (segment.end.X - p1.X) / int(2*PI*THETAFACTOR); // should be 1, -1 or 0
            if(wrapping_dir != 0)
            {
                segments[segment_idx].start.X += wrapping_dir*2*PI*THETAFACTOR;
                segments[segment_idx].end.X += wrapping_dir*2*PI*THETAFACTOR;
                // SlicerSegment temp;
                // temp.start = Point(segments[segment_idx].start.X + wrapping_dir*2*PI*THETAFACTOR, segments[segment_idx].start.Y);
                // temp.end = Point(segments[segment_idx].end.X + wrapping_dir*2*PI*THETAFACTOR, segments[segment_idx].end.Y);
                //segments[segment_idx] = temp;
            }
            if (segment_idx == static_cast<int>(start_segment_idx))
            {
                return start_segment_idx;
            }
            if (segments[segment_idx].addedToPolygon)
            {
                return -1;
            }
            return segment_idx;
        }
    }

    return -1;
}

int SlicerLayer::getNextSegmentIdx(SlicerSegment &segment, unsigned int start_segment_idx)
{
    int next_segment_idx = -1;

    bool segment_ended_at_edge = segment.endVertex == nullptr;
    if (segment_ended_at_edge)
    {
        int face_to_try = segment.endOtherFaceIdx;
        if (face_to_try == -1)
        {
            return -1;
        }
        return tryFaceNextSegmentIdx(segment, face_to_try, start_segment_idx);
    }
    else
    {
        // segment ended at vertex

        const std::vector<uint32_t> &faces_to_try = segment.endVertex->connected_faces;
        for (int face_to_try : faces_to_try)
        {
            int result_segment_idx =
                tryFaceNextSegmentIdx(segment, face_to_try, start_segment_idx);
            if (result_segment_idx == static_cast<int>(start_segment_idx))
            {
                return start_segment_idx;
            }
            else if (result_segment_idx != -1)
            {
                // not immediately returned since we might still encounter the start_segment_idx
                next_segment_idx = result_segment_idx;
            }
        }
    }

    return next_segment_idx;
}

void SlicerLayer::connectOpenPolylines(Polygons &open_polylines)
{
    bool allow_reverse = false;
    // Search a bit fewer cells but at cost of covering more area.
    // Since acceptance area is small to start with, the extra is unlikely to hurt much.
    coord_t cell_size = largest_neglected_gap_first_phase * 2;
    connectOpenPolylinesImpl(open_polylines, largest_neglected_gap_second_phase, cell_size, allow_reverse);
}

void SlicerLayer::stitch(Polygons &open_polylines)
{
    bool allow_reverse = true;
    connectOpenPolylinesImpl(open_polylines, max_stitch1, max_stitch1, allow_reverse);
}

const SlicerLayer::Terminus SlicerLayer::Terminus::INVALID_TERMINUS{~static_cast<Index>(0U)};

bool SlicerLayer::PossibleStitch::operator<(const PossibleStitch &other) const
{
    // better if lower distance
    if (dist2 > other.dist2)
    {
        return true;
    }
    else if (dist2 < other.dist2)
    {
        return false;
    }

    // better if in order instead of reversed
    if (!in_order() && other.in_order())
    {
        return true;
    }

    // better if lower Terminus::Index for terminus_0
    // This just defines a more total order and isn't strictly necessary.
    if (terminus_0.asIndex() > other.terminus_0.asIndex())
    {
        return true;
    }
    else if (terminus_0.asIndex() < other.terminus_0.asIndex())
    {
        return false;
    }

    // better if lower Terminus::Index for terminus_1
    // This just defines a more total order and isn't strictly necessary.
    if (terminus_1.asIndex() > other.terminus_1.asIndex())
    {
        return true;
    }
    else if (terminus_1.asIndex() < other.terminus_1.asIndex())
    {
        return false;
    }

    // The stitches have equal goodness
    return false;
}

std::priority_queue<SlicerLayer::PossibleStitch>
SlicerLayer::findPossibleStitches(
    const Polygons &open_polylines,
    coord_t max_dist, coord_t cell_size,
    bool allow_reverse) const
{
    std::priority_queue<PossibleStitch> stitch_queue;

    // maximum distance squared
    int64_t max_dist2 = max_dist * max_dist;

    // Represents a terminal point of a polyline in open_polylines.
    struct StitchGridVal
    {
        unsigned int polyline_idx;
        // Depending on the SparsePointGridInclusive, either the start point or the
        // end point of the polyline
        Point polyline_term_pt;
    };

    struct StitchGridValLocator
    {
        Point operator()(const StitchGridVal &val) const
        {
            return val.polyline_term_pt;
        }
    };

    // Used to find nearby end points within a fixed maximum radius
    SparsePointGrid<StitchGridVal, StitchGridValLocator> grid_ends(cell_size);
    // Used to find nearby start points within a fixed maximum radius
    SparsePointGrid<StitchGridVal, StitchGridValLocator> grid_starts(cell_size);

    // populate grids

    // Inserts the ends of all polylines into the grid (does not
    //   insert the starts of the polylines).
    for (unsigned int polyline_0_idx = 0; polyline_0_idx < open_polylines.size(); polyline_0_idx++)
    {
        ConstPolygonRef polyline_0 = open_polylines[polyline_0_idx];

        if (polyline_0.size() < 1)
            continue;

        StitchGridVal grid_val;
        grid_val.polyline_idx = polyline_0_idx;
        grid_val.polyline_term_pt = polyline_0.back();
        grid_ends.insert(grid_val);
    }

    // Inserts the start of all polylines into the grid.
    if (allow_reverse)
    {
        for (unsigned int polyline_0_idx = 0; polyline_0_idx < open_polylines.size(); polyline_0_idx++)
        {
            ConstPolygonRef polyline_0 = open_polylines[polyline_0_idx];

            if (polyline_0.size() < 1)
                continue;

            StitchGridVal grid_val;
            grid_val.polyline_idx = polyline_0_idx;
            grid_val.polyline_term_pt = polyline_0[0];
            grid_starts.insert(grid_val);
        }
    }

    // search for nearby end points
    for (unsigned int polyline_1_idx = 0; polyline_1_idx < open_polylines.size(); polyline_1_idx++)
    {
        ConstPolygonRef polyline_1 = open_polylines[polyline_1_idx];

        if (polyline_1.size() < 1)
            continue;

        std::vector<StitchGridVal> nearby_ends;

        // Check for stitches that append polyline_1 onto polyline_0
        // in natural order.  These are stitches that use the end of
        // polyline_0 and the start of polyline_1.
        nearby_ends = grid_ends.getNearby(polyline_1[0], max_dist);
        for (const auto &nearby_end : nearby_ends)
        {
            Point diff = nearby_end.polyline_term_pt - polyline_1[0];
            int64_t dist2 = vSize2(diff);
            if (dist2 < max_dist2)
            {
                PossibleStitch poss_stitch;
                poss_stitch.dist2 = dist2;
                poss_stitch.terminus_0 = Terminus{nearby_end.polyline_idx, true};
                poss_stitch.terminus_1 = Terminus{polyline_1_idx, false};
                stitch_queue.push(poss_stitch);
            }
        }

        if (allow_reverse)
        {
            // Check for stitches that append polyline_1 onto polyline_0
            // by reversing order of polyline_1.  These are stitches that
            // use the end of polyline_0 and the end of polyline_1.
            nearby_ends = grid_ends.getNearby(polyline_1.back(), max_dist);
            for (const auto &nearby_end : nearby_ends)
            {
                // Disallow stitching with self with same end point
                if (nearby_end.polyline_idx == polyline_1_idx)
                {
                    continue;
                }

                Point diff = nearby_end.polyline_term_pt - polyline_1.back();
                int64_t dist2 = vSize2(diff);
                if (dist2 < max_dist2)
                {
                    PossibleStitch poss_stitch;
                    poss_stitch.dist2 = dist2;
                    poss_stitch.terminus_0 = Terminus{nearby_end.polyline_idx, true};
                    poss_stitch.terminus_1 = Terminus{polyline_1_idx, true};
                    stitch_queue.push(poss_stitch);
                }
            }

            // Check for stitches that append polyline_1 onto polyline_0
            // by reversing order of polyline_0.  These are stitches that
            // use the start of polyline_0 and the start of polyline_1.
            std::vector<StitchGridVal> nearby_starts =
                grid_starts.getNearby(polyline_1[0], max_dist);
            for (const auto &nearby_start : nearby_starts)
            {
                // Disallow stitching with self with same end point
                if (nearby_start.polyline_idx == polyline_1_idx)
                {
                    continue;
                }

                Point diff = nearby_start.polyline_term_pt - polyline_1[0];
                int64_t dist2 = vSize2(diff);
                if (dist2 < max_dist2)
                {
                    PossibleStitch poss_stitch;
                    poss_stitch.dist2 = dist2;
                    poss_stitch.terminus_0 = Terminus{nearby_start.polyline_idx, false};
                    poss_stitch.terminus_1 = Terminus{polyline_1_idx, false};
                    stitch_queue.push(poss_stitch);
                }
            }
        }
    }

    return stitch_queue;
}

void SlicerLayer::planPolylineStitch(
    const Polygons &open_polylines,
    Terminus &terminus_0, Terminus &terminus_1, bool reverse[2]) const
{
    size_t polyline_0_idx = terminus_0.getPolylineIdx();
    size_t polyline_1_idx = terminus_1.getPolylineIdx();
    bool back_0 = terminus_0.isEnd();
    bool back_1 = terminus_1.isEnd();
    reverse[0] = false;
    reverse[1] = false;
    if (back_0)
    {
        if (back_1)
        {
            // back of both polylines
            // we can reverse either one and then append onto the other
            // reverse the smaller polyline
            if (open_polylines[polyline_0_idx].size() <
                open_polylines[polyline_1_idx].size())
            {
                std::swap(terminus_0, terminus_1);
            }
            reverse[1] = true;
        }
        else
        {
            // back of 0, front of 1
            // already in order, nothing to do
        }
    }
    else
    {
        if (back_1)
        {
            // front of 0, back of 1
            // in order if we swap 0 and 1
            std::swap(terminus_0, terminus_1);
        }
        else
        {
            // front of both polylines
            // we can reverse either one and then prepend to the other
            // reverse the smaller polyline
            if (open_polylines[polyline_0_idx].size() >
                open_polylines[polyline_1_idx].size())
            {
                std::swap(terminus_0, terminus_1);
            }
            reverse[0] = true;
        }
    }
}

void SlicerLayer::joinPolylines(PolygonRef &polyline_0, PolygonRef &polyline_1, const bool reverse[2]) const
{
    if (reverse[0])
    {
        // reverse polyline_0
        size_t size_0 = polyline_0.size();
        for (size_t idx = 0U; idx != size_0 / 2; ++idx)
        {
            std::swap(polyline_0[idx], polyline_0[size_0 - 1 - idx]);
        }
    }
    if (reverse[1])
    {
        // reverse polyline_1 by adding in reverse order
        for (int poly_idx = polyline_1.size() - 1; poly_idx >= 0; poly_idx--)
            polyline_0.add(polyline_1[poly_idx]);
    }
    else
    {
        // append polyline_1 onto polyline_0
        for (Point &p : polyline_1)
            polyline_0.add(p);
    }
    polyline_1.clear();
}

SlicerLayer::TerminusTrackingMap::TerminusTrackingMap(Terminus::Index end_idx) : m_terminus_old_to_cur_map(end_idx)
{
    // Initialize map to everything points to itself since nothing has moved yet.
    for (size_t idx = 0U; idx != end_idx; ++idx)
    {
        m_terminus_old_to_cur_map[idx] = Terminus{idx};
    }
    m_terminus_cur_to_old_map = m_terminus_old_to_cur_map;
}

void SlicerLayer::TerminusTrackingMap::updateMap(
    size_t num_terms,
    const Terminus *cur_terms, const Terminus *next_terms,
    size_t num_removed_terms,
    const Terminus *removed_cur_terms)
{
    // save old locations
    std::vector<Terminus> old_terms(num_terms);
    for (size_t idx = 0U; idx != num_terms; ++idx)
    {
        old_terms[idx] = getOldFromCur(cur_terms[idx]);
    }
    // update using maps old <-> cur and cur <-> next
    for (size_t idx = 0U; idx != num_terms; ++idx)
    {
        m_terminus_old_to_cur_map[old_terms[idx].asIndex()] = next_terms[idx];
        Terminus next_term = next_terms[idx];
        if (next_term != Terminus::INVALID_TERMINUS)
        {
            m_terminus_cur_to_old_map[next_term.asIndex()] = old_terms[idx];
        }
    }
    // remove next locations that no longer exist
    for (size_t rem_idx = 0U; rem_idx != num_removed_terms; ++rem_idx)
    {
        m_terminus_cur_to_old_map[removed_cur_terms[rem_idx].asIndex()] =
            Terminus::INVALID_TERMINUS;
    }
}

void SlicerLayer::connectOpenPolylinesImpl(Polygons &open_polylines, coord_t max_dist, coord_t cell_size, bool allow_reverse)
{
    // below code closes smallest gaps first

    std::priority_queue<PossibleStitch> stitch_queue =
        findPossibleStitches(open_polylines, max_dist, cell_size, allow_reverse);

    static const Terminus INVALID_TERMINUS = Terminus::INVALID_TERMINUS;
    Terminus::Index terminus_end_idx = Terminus::endIndexFromPolylineEndIndex(open_polylines.size());
    // Keeps track of how polyline end point locations move around
    TerminusTrackingMap terminus_tracking_map(terminus_end_idx);

    while (!stitch_queue.empty())
    {
        // Get the next best stitch
        PossibleStitch next_stitch;
        next_stitch = stitch_queue.top();
        stitch_queue.pop();
        Terminus old_terminus_0 = next_stitch.terminus_0;
        Terminus terminus_0 = terminus_tracking_map.getCurFromOld(old_terminus_0);
        if (terminus_0 == INVALID_TERMINUS)
        {
            // if we already used this terminus, then this stitch is no longer usable
            continue;
        }
        Terminus old_terminus_1 = next_stitch.terminus_1;
        Terminus terminus_1 = terminus_tracking_map.getCurFromOld(old_terminus_1);
        if (terminus_1 == INVALID_TERMINUS)
        {
            // if we already used this terminus, then this stitch is no longer usable
            continue;
        }

        size_t best_polyline_0_idx = terminus_0.getPolylineIdx();
        size_t best_polyline_1_idx = terminus_1.getPolylineIdx();

        // check to see if this completes a polygon
        bool completed_poly = best_polyline_0_idx == best_polyline_1_idx;
        if (completed_poly)
        {
            // finished polygon
            PolygonRef polyline_0 = open_polylines[best_polyline_0_idx];
            polygons.add(polyline_0);
            polyline_0.clear();
            Terminus cur_terms[2] = {{best_polyline_0_idx, false},
                                     {best_polyline_0_idx, true}};
            for (size_t idx = 0U; idx != 2U; ++idx)
            {
                terminus_tracking_map.markRemoved(cur_terms[idx]);
            }
            continue;
        }

        // we need to join these polylines

        // plan how to join polylines
        bool reverse[2];
        planPolylineStitch(open_polylines, terminus_0, terminus_1, reverse);

        // need to reread since planPolylineStitch can swap terminus_0/1
        best_polyline_0_idx = terminus_0.getPolylineIdx();
        best_polyline_1_idx = terminus_1.getPolylineIdx();
        PolygonRef polyline_0 = open_polylines[best_polyline_0_idx];
        PolygonRef polyline_1 = open_polylines[best_polyline_1_idx];

        // join polylines according to plan
        joinPolylines(polyline_0, polyline_1, reverse);

        // update terminus_tracking_map
        Terminus cur_terms[4] = {{best_polyline_0_idx, false},
                                 {best_polyline_0_idx, true},
                                 {best_polyline_1_idx, false},
                                 {best_polyline_1_idx, true}};
        Terminus next_terms[4] = {{best_polyline_0_idx, false},
                                  INVALID_TERMINUS,
                                  INVALID_TERMINUS,
                                  {best_polyline_0_idx, true}};
        if (reverse[0])
        {
            std::swap(next_terms[0], next_terms[1]);
        }
        if (reverse[1])
        {
            std::swap(next_terms[2], next_terms[3]);
        }
        // cur_terms -> next_terms has movement map
        // best_polyline_1 is always removed
        terminus_tracking_map.updateMap(4U, cur_terms, next_terms,
                                        2U, &cur_terms[2]);
    }
}

void SlicerLayer::stitch_extensive(Polygons &open_polylines)
{
    //For extensive stitching find 2 open polygons that are touching 2 closed polygons.
    // Then find the shortest path over this polygon that can be used to connect the open polygons,
    // And generate a path over this shortest bit to link up the 2 open polygons.
    // (If these 2 open polygons are the same polygon, then the final result is a closed polyon)

    while (1)
    {
        unsigned int best_polyline_1_idx = -1;
        unsigned int best_polyline_2_idx = -1;
        GapCloserResult best_result;
        best_result.len = POINT_MAX;
        best_result.polygonIdx = -1;
        best_result.pointIdxA = -1;
        best_result.pointIdxB = -1;

        for (unsigned int polyline_1_idx = 0; polyline_1_idx < open_polylines.size(); polyline_1_idx++)
        {
            PolygonRef polyline_1 = open_polylines[polyline_1_idx];
            if (polyline_1.size() < 1)
                continue;

            {
                GapCloserResult res = findPolygonGapCloser(polyline_1[0], polyline_1.back());
                if (res.len > 0 && res.len < best_result.len)
                {
                    best_polyline_1_idx = polyline_1_idx;
                    best_polyline_2_idx = polyline_1_idx;
                    best_result = res;
                }
            }

            for (unsigned int polyline_2_idx = 0; polyline_2_idx < open_polylines.size(); polyline_2_idx++)
            {
                PolygonRef polyline_2 = open_polylines[polyline_2_idx];
                if (polyline_2.size() < 1 || polyline_1_idx == polyline_2_idx)
                    continue;

                GapCloserResult res = findPolygonGapCloser(polyline_1[0], polyline_2.back());
                if (res.len > 0 && res.len < best_result.len)
                {
                    best_polyline_1_idx = polyline_1_idx;
                    best_polyline_2_idx = polyline_2_idx;
                    best_result = res;
                }
            }
        }

        if (best_result.len < POINT_MAX)
        {
            if (best_polyline_1_idx == best_polyline_2_idx)
            {
                if (best_result.pointIdxA == best_result.pointIdxB)
                {
                    polygons.add(open_polylines[best_polyline_1_idx]);
                    open_polylines[best_polyline_1_idx].clear();
                }
                else if (best_result.AtoB)
                {
                    PolygonRef poly = polygons.newPoly();
                    for (unsigned int j = best_result.pointIdxA; j != best_result.pointIdxB; j = (j + 1) % polygons[best_result.polygonIdx].size())
                        poly.add(polygons[best_result.polygonIdx][j]);
                    for (unsigned int j = open_polylines[best_polyline_1_idx].size() - 1; int(j) >= 0; j--)
                        poly.add(open_polylines[best_polyline_1_idx][j]);
                    open_polylines[best_polyline_1_idx].clear();
                }
                else
                {
                    unsigned int n = polygons.size();
                    polygons.add(open_polylines[best_polyline_1_idx]);
                    for (unsigned int j = best_result.pointIdxB; j != best_result.pointIdxA; j = (j + 1) % polygons[best_result.polygonIdx].size())
                        polygons[n].add(polygons[best_result.polygonIdx][j]);
                    open_polylines[best_polyline_1_idx].clear();
                }
            }
            else
            {
                if (best_result.pointIdxA == best_result.pointIdxB)
                {
                    for (unsigned int n = 0; n < open_polylines[best_polyline_1_idx].size(); n++)
                        open_polylines[best_polyline_2_idx].add(open_polylines[best_polyline_1_idx][n]);
                    open_polylines[best_polyline_1_idx].clear();
                }
                else if (best_result.AtoB)
                {
                    Polygon poly;
                    for (unsigned int n = best_result.pointIdxA; n != best_result.pointIdxB; n = (n + 1) % polygons[best_result.polygonIdx].size())
                        poly.add(polygons[best_result.polygonIdx][n]);
                    for (unsigned int n = poly.size() - 1; int(n) >= 0; n--)
                        open_polylines[best_polyline_2_idx].add(poly[n]);
                    for (unsigned int n = 0; n < open_polylines[best_polyline_1_idx].size(); n++)
                        open_polylines[best_polyline_2_idx].add(open_polylines[best_polyline_1_idx][n]);
                    open_polylines[best_polyline_1_idx].clear();
                }
                else
                {
                    for (unsigned int n = best_result.pointIdxB; n != best_result.pointIdxA; n = (n + 1) % polygons[best_result.polygonIdx].size())
                        open_polylines[best_polyline_2_idx].add(polygons[best_result.polygonIdx][n]);
                    for (unsigned int n = open_polylines[best_polyline_1_idx].size() - 1; int(n) >= 0; n--)
                        open_polylines[best_polyline_2_idx].add(open_polylines[best_polyline_1_idx][n]);
                    open_polylines[best_polyline_1_idx].clear();
                }
            }
        }
        else
        {
            break;
        }
    }
}

GapCloserResult SlicerLayer::findPolygonGapCloser(Point ip0, Point ip1)
{
    GapCloserResult ret;
    ClosePolygonResult c1 = findPolygonPointClosestTo(ip0);
    ClosePolygonResult c2 = findPolygonPointClosestTo(ip1);
    if (c1.polygonIdx < 0 || c1.polygonIdx != c2.polygonIdx)
    {
        ret.len = -1;
        return ret;
    }
    ret.polygonIdx = c1.polygonIdx;
    ret.pointIdxA = c1.pointIdx;
    ret.pointIdxB = c2.pointIdx;
    ret.AtoB = true;

    if (ret.pointIdxA == ret.pointIdxB)
    {
        //Connection points are on the same line segment.
        ret.len = vSize(ip0 - ip1);
    }
    else
    {
        //Find out if we have should go from A to B or the other way around.
        Point p0 = polygons[ret.polygonIdx][ret.pointIdxA];
        int64_t lenA = vSize(p0 - ip0);
        for (unsigned int i = ret.pointIdxA; i != ret.pointIdxB; i = (i + 1) % polygons[ret.polygonIdx].size())
        {
            Point p1 = polygons[ret.polygonIdx][i];
            lenA += vSize(p0 - p1);
            p0 = p1;
        }
        lenA += vSize(p0 - ip1);

        p0 = polygons[ret.polygonIdx][ret.pointIdxB];
        int64_t lenB = vSize(p0 - ip1);
        for (unsigned int i = ret.pointIdxB; i != ret.pointIdxA; i = (i + 1) % polygons[ret.polygonIdx].size())
        {
            Point p1 = polygons[ret.polygonIdx][i];
            lenB += vSize(p0 - p1);
            p0 = p1;
        }
        lenB += vSize(p0 - ip0);

        if (lenA < lenB)
        {
            ret.AtoB = true;
            ret.len = lenA;
        }
        else
        {
            ret.AtoB = false;
            ret.len = lenB;
        }
    }
    return ret;
}

ClosePolygonResult SlicerLayer::findPolygonPointClosestTo(Point input)
{
    ClosePolygonResult ret;
    for (unsigned int n = 0; n < polygons.size(); n++)
    {
        Point p0 = polygons[n][polygons[n].size() - 1];
        for (unsigned int i = 0; i < polygons[n].size(); i++)
        {
            Point p1 = polygons[n][i];

            //Q = A + Normal( B - A ) * ((( B - A ) dot ( P - A )) / VSize( A - B ));
            Point pDiff = p1 - p0;
            int64_t lineLength = vSize(pDiff);
            if (lineLength > 1)
            {
                int64_t distOnLine = dot(pDiff, input - p0) / lineLength;
                if (distOnLine >= 0 && distOnLine <= lineLength)
                {
                    Point q = p0 + pDiff * distOnLine / lineLength;
                    if (shorterThen(q - input, 100))
                    {
                        ret.intersectionPoint = q;
                        ret.polygonIdx = n;
                        ret.pointIdx = i;
                        return ret;
                    }
                }
            }
            p0 = p1;
        }
    }
    ret.polygonIdx = -1;
    return ret;
}

void SlicerLayer::makePolygons(const Mesh *mesh, bool keep_none_closed, bool extensive_stitching, bool is_initial_layer, int layer_nr)
{
    Polygons open_polylines;
    Polygon open_digon;

    makeBasicPolygonLoops(open_polylines, open_digon, mesh, layer_nr);

    connectOpenPolylines(open_polylines);

    // TODO: (?) for mesh surface mode: connect open polygons. Maybe the above algorithm can create two open polygons which are actually connected when the starting segment is in the middle between the two open polygons.

    if (mesh->getSettingAsSurfaceMode("magic_mesh_surface_mode") == ESurfaceMode::NORMAL)
    { // don't stitch when using (any) mesh surface mode, i.e. also don't stitch when using mixed mesh surface and closed polygons, because then polylines which are supposed to be open will be closed
        stitch(open_polylines);
    }

    if (extensive_stitching)
    {
        stitch_extensive(open_polylines);
    }

    if (keep_none_closed)
    {
        for (PolygonRef polyline : open_polylines)
        {
            if (polyline.size() > 0)
                polygons.add(polyline);
        }
    }

    for (PolygonRef polyline : open_polylines)
    {
        if (polyline.size() > 0)
        {
            openPolylines.add(polyline);
        }
    }

    //Remove all the tiny polygons, or polygons that are not closed. As they do not contribute to the actual print.
    int snapDistance = mesh->getSettingInMicrons("minimum_polygon_circumference");
    auto it = std::remove_if(polygons.begin(), polygons.end(), [snapDistance](PolygonRef poly) { return poly.shorterThan(snapDistance); });
    polygons.erase(it, polygons.end());

    //Finally optimize all the polygons. Every point removed saves time in the long run.
    const coord_t line_segment_resolution = mesh->getSettingInMicrons("meshfix_maximum_resolution");
    polygons.simplify(line_segment_resolution, line_segment_resolution / 2); //Maximum error is half of the resolution so it's only a limit when removing really sharp corners.

    polygons.removeDegenerateVerts(); // remove verts connected to overlapping line segments

    int xy_offset = mesh->getSettingInMicrons("xy_offset");
    if (is_initial_layer)
    {
        xy_offset = mesh->getSettingInMicrons("xy_offset_layer_0");
    }

    if (xy_offset != 0)
    {
        polygons = polygons.offset(xy_offset);
    }
}

bool binsearch(Point3 start, Point3 end, IntPoint cyl_axis, coord_t lim, uint depth, uint depth_lim)
{
    if (++depth > depth_lim)
        return false;
    else
    {
        Point3 pt = Point3((start.x + end.x) / 2, (start.y + end.y) / 2, (start.z + end.z) / 2);
        pt.toCylPoint3(cyl_axis.X, cyl_axis.Y);
        if (pt.cp->r < lim)
            return true;
        else
        {
            if (start.cp->r > pt.cp->r)
                start = pt;
            else if (end.cp->r > pt.cp->r)
                end = pt;
            return binsearch (start, end, cyl_axis, lim, depth, depth_lim);
        }
        
    }
}

Slicer::Slicer(Mesh *mesh, const coord_t initial_layer_thickness, const coord_t thickness, const size_t slice_layer_count, bool keep_none_closed, bool extensive_stitching,
               bool use_variable_layer_heights, std::vector<AdaptiveLayer> *adaptive_layers)
    : mesh(mesh)
{
    SlicingTolerance slicing_tolerance = mesh->getSettingAsSlicingTolerance("slicing_tolerance");

    assert(slice_layer_count > 0);

    TimeKeeper slice_timer;

    layers.resize(slice_layer_count);

    // set (and initialize compensation for) initial layer, depending on slicing mode
    layers[0].z = std::max(0LL, initial_layer_thickness - thickness);
    int adjusted_layer_offset = initial_layer_thickness;
    if (use_variable_layer_heights)
    {
        layers[0].z = adaptive_layers->at(0).z_position;
    }
    else if (slicing_tolerance == SlicingTolerance::MIDDLE)
    {
        layers[0].z = initial_layer_thickness / 2;
        adjusted_layer_offset = initial_layer_thickness + (thickness / 2);
    }

    // define all layer z positions (depending on slicing mode, see above)
    for (unsigned int layer_nr = 1; layer_nr < slice_layer_count; layer_nr++)
    {
        if (use_variable_layer_heights)
        {
            layers[layer_nr].z = adaptive_layers->at(layer_nr).z_position;
        }
        else
        {
            layers[layer_nr].z = adjusted_layer_offset + (thickness * (layer_nr - 1));
        }
    }

    // loop over all mesh faces
    for (unsigned int mesh_idx = 0; mesh_idx < mesh->faces.size(); mesh_idx++)
    {
        // get all vertices per face
        const MeshFace &face = mesh->faces[mesh_idx];
        const MeshVertex &v0 = mesh->vertices[face.vertex_index[0]];
        const MeshVertex &v1 = mesh->vertices[face.vertex_index[1]];
        const MeshVertex &v2 = mesh->vertices[face.vertex_index[2]];

        // get all vertices represented as 3D point
        Point3 p0 = v0.p;
        Point3 p1 = v1.p;
        Point3 p2 = v2.p;

        // find the minimum and maximum z point
        int32_t minZ = p0.z;
        int32_t maxZ = p0.z;
        if (p1.z < minZ)
            minZ = p1.z;
        if (p2.z < minZ)
            minZ = p2.z;
        if (p1.z > maxZ)
            maxZ = p1.z;
        if (p2.z > maxZ)
            maxZ = p2.z;

        // calculate all intersections between a layer plane and a triangle
        for (unsigned int layer_nr = 0; layer_nr < layers.size(); layer_nr++)
        {
            int32_t z = layers.at(layer_nr).z;

            if (z < minZ)
                continue;

            SlicerSegment s;
            s.endVertex = nullptr;
            int end_edge_idx = -1;

            if (p0.z < z && p1.z >= z && p2.z >= z)
            {
                s = project2D(p0, p2, p1, z);
                end_edge_idx = 0;
                if (p1.z == z)
                {
                    s.endVertex = &v1;
                }
            }
            else if (p0.z > z && p1.z < z && p2.z < z)
            {
                s = project2D(p0, p1, p2, z);
                end_edge_idx = 2;
            }
            else if (p1.z < z && p0.z >= z && p2.z >= z)
            {
                s = project2D(p1, p0, p2, z);
                end_edge_idx = 1;
                if (p2.z == z)
                {
                    s.endVertex = &v2;
                }
            }
            else if (p1.z > z && p0.z < z && p2.z < z)
            {
                s = project2D(p1, p2, p0, z);
                end_edge_idx = 0;
            }
            else if (p2.z < z && p1.z >= z && p0.z >= z)
            {
                s = project2D(p2, p1, p0, z);
                end_edge_idx = 2;
                if (p0.z == z)
                {
                    s.endVertex = &v0;
                }
            }
            else if (p2.z > z && p1.z < z && p0.z < z)
            {
                s = project2D(p2, p0, p1, z);
                end_edge_idx = 1;
            }
            else
            {
                //Not all cases create a segment, because a point of a face could create just a dot, and two touching faces
                //  on the slice would create two segments
                continue;
            }

            // store the segments per layer
            layers[layer_nr].face_idx_to_segment_idx.insert(std::make_pair(mesh_idx, layers[layer_nr].segments.size()));
            s.faceIndex = mesh_idx;
            s.endOtherFaceIdx = face.connected_face_index[end_edge_idx];
            s.addedToPolygon = false;
            layers[layer_nr].segments.push_back(s);
        }
    }

    log("slice of mesh took %.3f seconds\n", slice_timer.restart());

    std::vector<SlicerLayer> &layers_ref = layers; // force layers not to be copied into the threads

#pragma omp parallel for default(none) shared(mesh, layers_ref) firstprivate(keep_none_closed, extensive_stitching)
    for (unsigned int layer_nr = 0; layer_nr < layers_ref.size(); layer_nr++)
    {
        layers_ref[layer_nr].makePolygons(mesh, keep_none_closed, extensive_stitching, layer_nr == 0, layer_nr);
    }

    switch (slicing_tolerance)
    {
    case SlicingTolerance::INCLUSIVE:
        for (unsigned int layer_nr = 0; layer_nr + 1 < layers_ref.size(); layer_nr++)
        {
            layers[layer_nr].polygons = layers[layer_nr].polygons.unionPolygons(layers[layer_nr + 1].polygons);
        }
        break;
    case SlicingTolerance::EXCLUSIVE:
        for (unsigned int layer_nr = 0; layer_nr + 1 < layers_ref.size(); layer_nr++)
        {
            layers[layer_nr].polygons = layers[layer_nr].polygons.intersection(layers[layer_nr + 1].polygons);
        }
        layers.back().polygons.clear();
        break;
    case SlicingTolerance::MIDDLE:
    default:
        // do nothing
        ;
    }

    mesh->expandXY(mesh->getSettingInMicrons("xy_offset"));
    log("slice make polygons took %.3f seconds\n", slice_timer.restart());
}

Slicer::Slicer(Mesh *mesh, const coord_t initial_layer_thickness, const coord_t thickness, const size_t slice_layer_count, bool keep_none_closed, bool extensive_stitching,
               bool use_variable_layer_heights, std::vector<AdaptiveLayer> *adaptive_layers, IntPoint cyl_axis, coord_t drum_r)
    // added cyl_axis, for now it's assumed that it's a vertical line
    : mesh(mesh)
{
    SlicingTolerance slicing_tolerance = mesh->getSettingAsSlicingTolerance("slicing_tolerance");

    assert(slice_layer_count > 0);

    TimeKeeper slice_timer;

    layers.resize(slice_layer_count);

    // set (and initialize compensation for) initial layer, depending on slicing mode
    layers[0].z = std::max(0LL, initial_layer_thickness - thickness);
    int adjusted_layer_offset = initial_layer_thickness;
    if (use_variable_layer_heights)
    {
        layers[0].z = adaptive_layers->at(0).z_position;
    }
    else if (slicing_tolerance == SlicingTolerance::MIDDLE)
    {
        layers[0].z = initial_layer_thickness / 2 + drum_r;
        adjusted_layer_offset = initial_layer_thickness + (thickness / 2) + drum_r;
    }

    // define all layer z positions (depending on slicing mode, see above)
    for (unsigned int layer_nr = 1; layer_nr < slice_layer_count; layer_nr++)
    {
        if (use_variable_layer_heights)
        {
            layers[layer_nr].z = adaptive_layers->at(layer_nr).z_position;
        }
        else
        {
            layers[layer_nr].z = adjusted_layer_offset + (thickness * (layer_nr - 1));
        }
    }

    // loop over all mesh faces
    for (unsigned int mesh_idx = 0; mesh_idx < mesh->faces.size(); mesh_idx++)
    {
        // get all vertices per face
        const MeshFace &face = mesh->faces[mesh_idx];
        const MeshVertex &v0 = mesh->vertices[face.vertex_index[0]];
        const MeshVertex &v1 = mesh->vertices[face.vertex_index[1]];
        const MeshVertex &v2 = mesh->vertices[face.vertex_index[2]];

        // get all vertices represented as 3D point
        Point3 p0 = v0.p;
        Point3 p1 = v1.p;
        Point3 p2 = v2.p;

        // find the minimum and maximum R value
        CylPoint3 cyl_p0 = *p0.cp;
        CylPoint3 cyl_p1 = *p1.cp;
        CylPoint3 cyl_p2 = *p2.cp;

        float minR = cyl_p0.r;
        float maxR = cyl_p0.r;
        if (cyl_p1.r < minR) minR = cyl_p1.r;
        if (cyl_p2.r < minR) minR = cyl_p2.r;
        if (cyl_p1.r > maxR) maxR = cyl_p1.r;
        if (cyl_p2.r > maxR) maxR = cyl_p2.r;

        // find the perpendicular distance between triangle edges and the cyl_axis
        // TODO probably pack this into a function

        coord_t d_p0p1 = dist2axis(p0, p1, cyl_axis);
        coord_t d_p1p2 = dist2axis(p1, p2, cyl_axis);
        coord_t d_p2p0 = dist2axis(p2, p0, cyl_axis);

        // double delX = p1.x - p0.x;
        // double delZ = p1.z - p0.z;
        // double relX = p0.x - cyl_axis.X;
        // double relZ = p0.z - cyl_axis.Y; // cyl_axis is along Y axis so Y coordinate is cartesian Z
        // double dotcross = delX*relZ - delZ*relX;
        // double mag = sqrt(pow(delZ,2) + pow(delX,2));

        // double d_p0p1; 
        // // if (dotcross == 0 ) 
        // //     d_p0p1 = std::min(cyl_p0.r, cyl_p1.r);
        // // else
        //     d_p0p1 = abs(dotcross / mag);

        // delX = p2.x - p1.x;
        // delZ = p2.z - p1.z;
        // relX = p1.x - cyl_axis.X;
        // relZ = p1.z - cyl_axis.Y;
        // dotcross = delX*relZ - delZ*relX;
        // mag = sqrt(pow(delZ,2) + pow(delX,2));
        
        // double d_p1p2; 
        // // if (dotcross == 0 ) 
        // //     d_p1p2 = std::min(cyl_p1.r, cyl_p2.r);
        // // else
        //     d_p1p2 = abs(dotcross / mag);

        // delX = p0.x - p2.x;
        // delZ = p0.z - p2.z;
        // relX = p2.x - cyl_axis.X;
        // relZ = p2.z - cyl_axis.Y;
        // dotcross = delX*relZ - delZ*relX;
        // mag = sqrt(pow(delZ,2) + pow(delX,2));

        // double d_p2p0; 
        // // if (dotcross == 0 ) 
        // //     d_p2p0 = std::min(cyl_p2.r, cyl_p0.r);
        // // else
        //     d_p2p0 = abs(dotcross / mag);

        double minD = d_p0p1;
        if (d_p1p2 < minD) minD = d_p1p2;
        if (d_p2p0 < minD) minD = d_p2p0;
        double maxD = d_p0p1;
        if (d_p1p2 > maxD) maxD = d_p1p2;
        if (d_p2p0 > maxD) maxD = d_p2p0;

        // calculate all intersections between a layer plane and a triangle
        for (unsigned int layer_nr = 0; layer_nr < layers.size(); layer_nr++)
        {
            int32_t r = layers.at(layer_nr).z;
            std::vector<Point> points_on_cyl;

            // current radius is below any points and distances
            if (r < minR && r < minD)
                continue; // I think is r < minD then r < minR is always true?
            if (r > maxR && r > maxD)
                continue; // weirdly not in the original engine source

            int numPointsIn = 0;
            if (cyl_p0.r <= r) numPointsIn++;
            if (cyl_p1.r <= r) numPointsIn++;
            if (cyl_p2.r <= r) numPointsIn++;

            int numEdgesIn = 0;
            if (d_p0p1 <= r) numEdgesIn++;
            if (d_p1p2 <= r) numEdgesIn++;
            if (d_p2p0 <= r) numEdgesIn++;

            SlicerSegment s;
            s.endVertex = nullptr;
            std::vector<int> end_edge_idxs;
            CylSolver *cs1, *cs2, *cs3;

            if (numPointsIn == 2)
            {
                //case 3.2
                if (cyl_p0.r > r)
                {
                    // point 0 is out, run cs on p2p0 and p0p1
                    cs1 = new CylSolver(p2, p0, r, cyl_axis);
                    cs2 = new CylSolver(p0, p1, r, cyl_axis);
                    end_edge_idxs.push_back(0);
                }
                else if (cyl_p1.r > r)
                {
                    //point 1 is out, run cs on p0p1, p1p2
                    cs1 = new CylSolver(p0, p1, r, cyl_axis);
                    cs2 = new CylSolver(p1, p2, r, cyl_axis);
                    end_edge_idxs.push_back(1);
                }
                else if (cyl_p2.r > r)
                {
                    //point 2 is out, run cs on p12, p2p0
                    cs1 = new CylSolver(p1, p2, r, cyl_axis);
                    cs2 = new CylSolver(p2, p0, r, cyl_axis);
                    end_edge_idxs.push_back(2);
                }
                
                points_on_cyl.push_back(*cs1->itx_either);
                points_on_cyl.push_back(*cs2->itx_either);

            }   // numPoints in == 2
            else if (numPointsIn == 1)
            {

                if (numEdgesIn == 3)
                {
                    //case 3.1, generates two line segments
                    CylSolver *cs1;
                    if (cyl_p0.r <= r) 
                    {
                        //  p0 is in, edge12 is in, run cs on p0p1, p1p2, p2p0, make 1,2,1 points
                        cs1 = new CylSolver(p0, p1, r, cyl_axis);
                        cs2 = new CylSolver(p1, p2, r, cyl_axis);
                        cs3 = new CylSolver(p2, p0, r, cyl_axis);
                        end_edge_idxs.push_back(1);
                        end_edge_idxs.push_back(2);
                    }
                    else if (cyl_p1.r <= r) 
                    {
                        // edge p2p0 is in,p1 is in (hopefully)
                        cs1 = new CylSolver(p1, p2, r, cyl_axis);
                        cs2 = new CylSolver(p2, p0, r, cyl_axis);
                        cs3 = new CylSolver(p0, p1, r, cyl_axis);
                        end_edge_idxs.push_back(2);
                        end_edge_idxs.push_back(0);
                    }
                    else if (cyl_p2.r <= r)
                    {                        
                        // edge p0p1 is in,p2 is in (hopefully)
                        cs1 = new CylSolver(p2, p0, r, cyl_axis);
                        cs2 = new CylSolver(p0, p1, r, cyl_axis);
                        cs3 = new CylSolver(p1, p2, r, cyl_axis);
                        end_edge_idxs.push_back(0);
                        end_edge_idxs.push_back(1);
                    }
                    else
                    {
                        continue;
                    }
                    

                    points_on_cyl.push_back(*cs1->itx_either);
                    points_on_cyl.push_back(*cs2->itx_p1);
                    points_on_cyl.push_back(*cs2->itx_p2);
                    points_on_cyl.push_back(*cs3->itx_either);
                }
                else if (numEdgesIn == 2)
                {
                    // case 2.1
                    if (cyl_p0.r <= r)
                    {
                        // point 0 is in, run cs on p0p1, p2p0, solutions will be equal for each cs
                        cs1 = new CylSolver(p0, p1, r, cyl_axis);
                        cs2 = new CylSolver(p2, p0, r, cyl_axis);
                        end_edge_idxs.push_back(2);
                    }
                    else if (cyl_p1.r <= r)
                    {
                        //point 1 is in
                        cs1 = new CylSolver(p1, p2, r, cyl_axis);
                        cs2 = new CylSolver(p0, p1, r, cyl_axis);
                        end_edge_idxs.push_back(0);
                    }
                    else if (cyl_p2.r <= r)
                    {
                        //point 2 is in
                        cs1 = new CylSolver(p2, p0, r, cyl_axis);
                        cs2 = new CylSolver(p1, p2, r, cyl_axis);
                        end_edge_idxs.push_back(1);
                    }
                    else
                    {
                        continue;
                    }
                    
                    points_on_cyl.push_back(*cs1->itx_either);
                    points_on_cyl.push_back(*cs2->itx_either);

                }
                else
                    assert(false); // should not happen
            } // numPointsIn == 1
            else if (numPointsIn == 0)
            {
                if (numEdgesIn == 3)
                {
                // all edges in, run cs on p0p1, p1p2, p2p0
                    cs1 = new CylSolver(p0, p1, r, cyl_axis);
                    cs2 = new CylSolver(p1, p2, r, cyl_axis);
                    cs3 = new CylSolver(p2, p0, r, cyl_axis);

                    points_on_cyl.push_back(*cs1->itx_p2);
                    points_on_cyl.push_back(*cs2->itx_p1);
                    points_on_cyl.push_back(*cs2->itx_p2);
                    points_on_cyl.push_back(*cs3->itx_p1);
                    points_on_cyl.push_back(*cs3->itx_p2);
                    points_on_cyl.push_back(*cs1->itx_p1);
                    end_edge_idxs.push_back(1);
                    end_edge_idxs.push_back(2);
                    end_edge_idxs.push_back(0);
                }

                else if (numEdgesIn == 2)
                {
                    //case 2.0
                    if (d_p0p1 > r)
                    {
                        // edge p0p1 is out, run cs on p2p0, p1p2
                        cs1 = new CylSolver(p1, p2, r, cyl_axis);
                        cs2 = new CylSolver(p2, p0, r, cyl_axis);
                        end_edge_idxs.push_back(2);
                        end_edge_idxs.push_back(1);
                    }
                    else if (d_p1p2 > r)
                    {
                        // edge p1p2 is out, run cs on p0p1, p2p0
                        cs1 = new CylSolver(p2, p0, r, cyl_axis);
                        cs2 = new CylSolver(p0, p1, r, cyl_axis);
                        end_edge_idxs.push_back(0);
                        end_edge_idxs.push_back(2);
                    }
                    else if (d_p2p0 > r)
                    {
                        // edge p2p0 is out, run cs on p1p2, p0p1
                        cs1 = new CylSolver(p0, p1, r, cyl_axis);
                        cs2 = new CylSolver(p1, p2, r, cyl_axis);
                        end_edge_idxs.push_back(1);
                        end_edge_idxs.push_back(0);
                    }
                    else
                    {
                        continue;
                    }
                    points_on_cyl.push_back(*cs1->itx_p2);
                    points_on_cyl.push_back(*cs2->itx_p1);
                    points_on_cyl.push_back(*cs2->itx_p2);
                    points_on_cyl.push_back(*cs1->itx_p1);
                }

                else if (numEdgesIn == 1)
                {
                    //case 1.0
                    if (d_p0p1 <= r)
                    {
                        //edge p0p1 is in, run cs on p0p1
                        cs1 = new CylSolver(p0, p1, r, cyl_axis);
                        end_edge_idxs.push_back(0);
                    }
                    else if (d_p1p2 <= r)
                    {
                        // edge p1p2 is in, run cs on p1p2
                        cs1 = new CylSolver(p1, p2, r, cyl_axis);
                        end_edge_idxs.push_back(1);
                    }
                    else if (d_p2p0 <= r)
                    {
                        // edge p2p0 is in, run cs on p2p0
                        cs1 = new CylSolver(p2, p0, r, cyl_axis);
                        end_edge_idxs.push_back(2);
                    }
                    else
                    {
                        continue;
                    }
                    
                    points_on_cyl.push_back(*cs1->itx_p2);
                    points_on_cyl.push_back(*cs1->itx_p1);
                }
                else
                {
                // the very rare completely surrounds cylinder case. still no plan
                assert(false);
                }
            } // numPointsIn == 0

            // make a line segments with theta, and Y values
            assert(points_on_cyl.size() % 2 == 0);

            for (int line_seg_num = 0; line_seg_num < points_on_cyl.size(); line_seg_num += 2)
            {
                // segments are saved as theta, Y values.
                // segments are converted into "flat drum surface" coordinates in WallsComputation
                s.start = points_on_cyl[line_seg_num];
                s.end = points_on_cyl[line_seg_num + 1];

                // deal with segments that go over PI or -PI 
                if ((s.start.X  - s.end.X ) > PI*THETAFACTOR) // going + theta
                {
                    s.end.X += 2*PI*THETAFACTOR;
                }
                else if ((s.end.X - s.start.X ) > PI*THETAFACTOR) // going -theta
                {
                    s.start.X += 2*PI*THETAFACTOR;
                }

                // store each segment
                layers[layer_nr].face_idx_to_segment_idx.insert(std::make_pair(mesh_idx, layers[layer_nr].segments.size()));
                s.faceIndex = mesh_idx;
                s.endOtherFaceIdx = face.connected_face_index[end_edge_idxs[line_seg_num / 2]];
                s.addedToPolygon = false;
                layers[layer_nr].segments.push_back(s);
            }
        }
    }

    log("slice of mesh took %.3f seconds\n", slice_timer.restart());

    std::vector<SlicerLayer> &layers_ref = layers; // force layers not to be copied into the threads

#pragma omp parallel for default(none) shared(mesh, layers_ref) firstprivate(keep_none_closed, extensive_stitching)
    for (unsigned int layer_nr = 0; layer_nr < layers_ref.size(); layer_nr++)
    {
        layers_ref[layer_nr].makePolygons(mesh, keep_none_closed, extensive_stitching, layer_nr == 0, layer_nr);
    }

    switch (slicing_tolerance)
    {
    case SlicingTolerance::INCLUSIVE:
        for (unsigned int layer_nr = 0; layer_nr + 1 < layers_ref.size(); layer_nr++)
        {
            layers[layer_nr].polygons = layers[layer_nr].polygons.unionPolygons(layers[layer_nr + 1].polygons);
        }
        break;
    case SlicingTolerance::EXCLUSIVE:
        for (unsigned int layer_nr = 0; layer_nr + 1 < layers_ref.size(); layer_nr++)
        {
            layers[layer_nr].polygons = layers[layer_nr].polygons.intersection(layers[layer_nr + 1].polygons);
        }
        layers.back().polygons.clear();
        break;
    case SlicingTolerance::MIDDLE:
    default:
        // do nothing
        ;
    }

    mesh->expandXY(mesh->getSettingInMicrons("xy_offset"));
    log("slice make polygons took %.3f seconds\n", slice_timer.restart());
}

} //namespace cura