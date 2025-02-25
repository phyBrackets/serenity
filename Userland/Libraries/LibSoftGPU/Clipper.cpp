/*
 * Copyright (c) 2021, Jesse Buhagiar <jooster669@gmail.com>
 * Copyright (c) 2021, Stephan Unverwerth <s.unverwerth@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Format.h>
#include <AK/ScopeGuard.h>
#include <LibSoftGPU/Clipper.h>

namespace SoftGPU {

bool Clipper::point_within_clip_plane(FloatVector4 const& vertex, ClipPlane plane) const
{
    switch (plane) {
    case ClipPlane::LEFT:
        return vertex.x() >= -vertex.w();
    case ClipPlane::RIGHT:
        return vertex.x() <= vertex.w();
    case ClipPlane::TOP:
        return vertex.y() <= vertex.w();
    case ClipPlane::BOTTOM:
        return vertex.y() >= -vertex.w();
    case ClipPlane::NEAR:
        return vertex.z() >= -vertex.w();
    case ClipPlane::FAR:
        return vertex.z() <= vertex.w();
    }

    return false;
}

Vertex Clipper::clip_intersection_point(Vertex const& p1, Vertex const& p2, ClipPlane plane_index) const
{
    // See https://www.microsoft.com/en-us/research/wp-content/uploads/1978/01/p245-blinn.pdf
    // "Clipping Using Homogeneous Coordinates" Blinn/Newell, 1978

    float const w1 = p1.clip_coordinates.w();
    float const w2 = p2.clip_coordinates.w();
    float const x1 = clip_plane_normals[plane_index].dot(p1.clip_coordinates);
    float const x2 = clip_plane_normals[plane_index].dot(p2.clip_coordinates);
    float const a = (w1 + x1) / ((w1 + x1) - (w2 + x2));

    Vertex out;
    out.position = mix(p1.position, p2.position, a);
    out.eye_coordinates = mix(p1.eye_coordinates, p2.eye_coordinates, a);
    out.clip_coordinates = mix(p1.clip_coordinates, p2.clip_coordinates, a);
    out.color = mix(p1.color, p2.color, a);
    for (size_t i = 0; i < NUM_SAMPLERS; ++i)
        out.tex_coords[i] = mix(p1.tex_coords[i], p2.tex_coords[i], a);
    out.normal = mix(p1.normal, p2.normal, a);
    return out;
}

void Clipper::clip_triangle_against_frustum(Vector<Vertex>& input_verts)
{
    list_a = input_verts;
    list_b.clear_with_capacity();

    auto read_from = &list_a;
    auto write_to = &list_b;

    for (size_t plane = 0; plane < NUMBER_OF_CLIPPING_PLANES; plane++) {
        write_to->clear_with_capacity();
        // Save me, C++23
        for (size_t i = 0; i < read_from->size(); i++) {
            auto const& curr_vec = read_from->at((i + 1) % read_from->size());
            auto const& prev_vec = read_from->at(i);

            if (point_within_clip_plane(curr_vec.clip_coordinates, static_cast<ClipPlane>(plane))) {
                if (!point_within_clip_plane(prev_vec.clip_coordinates, static_cast<ClipPlane>(plane))) {
                    auto const intersect = clip_intersection_point(prev_vec, curr_vec, static_cast<ClipPlane>(plane));
                    write_to->append(intersect);
                }
                write_to->append(curr_vec);
            } else if (point_within_clip_plane(prev_vec.clip_coordinates, static_cast<ClipPlane>(plane))) {
                auto const intersect = clip_intersection_point(prev_vec, curr_vec, static_cast<ClipPlane>(plane));
                write_to->append(intersect);
            }
        }
        swap(write_to, read_from);
    }

    input_verts = *read_from;
}
}
