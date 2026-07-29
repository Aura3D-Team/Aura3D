#ifndef AURA_MESH_LOADER_H
#define AURA_MESH_LOADER_H

#pragma once

#include <string>

#include "aura/Core/AuraCore.h"

namespace aura3d {

/**
 * @class MeshLoader
 * @brief Produces gfx::Mesh3D geometry from disk or from built-in primitives.
 *
 * The OBJ parser is an implementation detail, so this header stays free of
 * third-party includes and can remain part of the installed public API.
 */
class MeshLoader {
public:
    /**
     * @brief Loads a Wavefront OBJ file.
     *
     * Positions, normals and texture coordinates are read; faces are
     * triangulated. Vertices are de-duplicated, so shared corners become shared
     * indices rather than repeated data. Meshes without normals get flat
     * per-face normals generated so lighting still works.
     *
     * Never throws: a missing or unparseable file logs a warning and returns
     * createCube(), matching the texture loader's fallback strategy.
     */
    static gfx::Mesh3D loadOBJ(const std::string& path);

    //! Unit cube centred on the origin, with per-face normals and UVs.
    static gfx::Mesh3D createCube();

    /**
     * @brief UV sphere of radius 0.5 centred on the origin.
     * @param subdivisions Tessellation level; clamped to at least 1.
     */
    static gfx::Mesh3D createSphere(int subdivisions = 2);

    //! Unit quad on the XZ plane, facing +Y.
    static gfx::Mesh3D createPlane();
};

} // namespace aura3d

#endif // AURA_MESH_LOADER_H
