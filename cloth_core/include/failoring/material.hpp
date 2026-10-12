#pragma once
#include <string>
#include <vector>

#include "failoring/math.hpp"

namespace fl {

// Fabric parameters in SI units, written so they can be compared directly with
// textile-science measurements (Kawabata KES-F, FAST, Cusick drape, cantilever).
//
//  - "warp" (u) is the lengthwise yarn direction (along the selvedge), "weft"
//    (v) is crosswise. For knits read warp = wales, weft = courses.
//  - Stiffnesses are per unit WIDTH of fabric, so they do not depend on how
//    finely the mesh is subdivided.
struct FabricMaterial {
    std::string id;
    std::string name;
    std::string category;  // woven | knit | leather | nonwoven ...
    std::string notes;

    Real density = 0.15;        // kg / m^2  (areal density; 150 g/m^2 = 0.15)
    Real thickness = 0.0005;    // m, used for contact offsets
    Real tensileWarp = 3000.0;  // N/m   in-plane stretch stiffness along warp
    Real tensileWeft = 3000.0;  // N/m   ... along weft
    Real shear = 40.0;          // N/m per radian of shear angle
    Real bendWarp = 1.0e-5;     // N*m   bending rigidity per unit width, curvature along warp
    Real bendWeft = 1.0e-5;     // N*m   ... curvature along weft
    Real compressionRatio = 0.3;  // in-plane compression stiffness / tension stiffness (0..1)
    Real frictionStatic = 0.5;  // cloth-vs-body/other friction
    Real frictionKinetic = 0.35;
    Real damping = 0.6;         // 1/s, viscous velocity damping (air + internal)
};

// Parse a materials file (see materials/fabrics.json). Throws std::runtime_error
// on I/O or format problems.
std::vector<FabricMaterial> loadMaterialsFromFile(const std::string& path);
std::vector<FabricMaterial> loadMaterialsFromString(const std::string& jsonText);

const FabricMaterial* findMaterial(const std::vector<FabricMaterial>& list, const std::string& id);

}  // namespace fl
