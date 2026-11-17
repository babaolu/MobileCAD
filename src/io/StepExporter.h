#pragma once

#include <android/log.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "rendering/MeshBuffer.h"
#include "utils/math/Mat4.h"
#include "utils/math/Vec3.h"

namespace io
{

class StepExporter
{
   public:
    template <typename SceneObjectType>
    static bool exportSTEP(const std::string& filepath, const std::vector<SceneObjectType>& objects,
                           const std::string& modelName = "MobileCAD_Model")
    {
        if (objects.empty())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "STEP export failed: Scene is empty");
            return false;
        }

        try
        {
            std::filesystem::path p(filepath);
            if (p.has_parent_path())
            {
                std::filesystem::create_directories(p.parent_path());
            }
        }
        catch (const std::exception& e)
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD", "Failed to create directory: %s",
                                e.what());
        }

        std::ofstream file(filepath, std::ios::trunc);
        if (!file.is_open())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "STEP export failed: cannot open %s", filepath.c_str());
            return false;
        }

        // Get current ISO 8601 timestamp
        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm tmBuf{};
        gmtime_r(&nowTime, &tmBuf);
        char timeStr[64];
        std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%SZ", &tmBuf);

        std::string fileNameOnly = std::filesystem::path(filepath).filename().string();
        if (fileNameOnly.empty())
            fileNameOnly = modelName + ".step";

        // 1. ISO-10303-21 Header
        file << "ISO-10303-21;\n";
        file << "HEADER;\n";
        file << "FILE_DESCRIPTION(('MobileCAD ISO-10303-21 STEP AP214 Solid Model'),'2;1');\n";
        file << "FILE_NAME('" << fileNameOnly << "','" << timeStr
             << "',('MobileCAD User'),('MobileCAD CAD/CAM'),'MobileCAD 1.4 Native STEP Generator',"
                "'MobileCAD','MobileCAD Authorization');\n";
        file << "FILE_SCHEMA(('AUTOMOTIVE_DESIGN'));\n";
        file << "ENDSEC;\n";

        // 2. DATA Section
        file << "DATA;\n";

        uint64_t nextId = 1;

        // Context and units
        uint64_t idAppContext = nextId++;
        file << "#" << idAppContext << " = APPLICATION_CONTEXT('automotive design');\n";

        uint64_t idAppProtocol = nextId++;
        file << "#" << idAppProtocol
             << " = APPLICATION_PROTOCOL_DEFINITION('draft international "
                "standard','automotive_design',2000,#"
             << idAppContext << ");\n";

        uint64_t idProductContext = nextId++;
        file << "#" << idProductContext << " = PRODUCT_CONTEXT('',#" << idAppContext
             << ",'mechanical');\n";

        uint64_t idProduct = nextId++;
        file << "#" << idProduct << " = PRODUCT('" << modelName << "','" << modelName << "','',(#"
             << idProductContext << "));\n";

        uint64_t idProdFormation = nextId++;
        file << "#" << idProdFormation << " = PRODUCT_DEFINITION_FORMATION('','',#" << idProduct
             << ");\n";

        uint64_t idProdDefContext = nextId++;
        file << "#" << idProdDefContext << " = PRODUCT_DEFINITION_CONTEXT('part definition',#"
             << idAppContext << ",'design');\n";

        uint64_t idProdDef = nextId++;
        file << "#" << idProdDef << " = PRODUCT_DEFINITION('design','',#" << idProdFormation << ",#"
             << idProdDefContext << ");\n";

        uint64_t idProdDefShape = nextId++;
        file << "#" << idProdDefShape << " = PRODUCT_DEFINITION_SHAPE('','',#" << idProdDef
             << ");\n";

        // SI Units: Millimeters (length) and Radians (plane angle)
        uint64_t idLengthUnit = nextId++;
        file << "#" << idLengthUnit
             << " = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );\n";

        uint64_t idAngleUnit = nextId++;
        file << "#" << idAngleUnit
             << " = ( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) );\n";

        uint64_t idSolidAngleUnit = nextId++;
        file << "#" << idSolidAngleUnit
             << " = ( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() );\n";

        uint64_t idUncertainty = nextId++;
        file << "#" << idUncertainty << " = UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.0E-04),#"
             << idLengthUnit << ",'distance_accuracy_value','confusion accuracy');\n";

        uint64_t idGeomContext = nextId++;
        file << "#" << idGeomContext
             << " = ( GEOMETRIC_REPRESENTATION_CONTEXT(3) "
                "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#"
             << idUncertainty << ")) GLOBAL_UNIT_ASSIGNED_CONTEXT((#" << idLengthUnit << ",#"
             << idAngleUnit << ",#" << idSolidAngleUnit
             << ")) REPRESENTATION_CONTEXT('Context #1','3D Context with UNIT') );\n";

        std::vector<uint64_t> solidBrepIds;

        file << std::fixed << std::setprecision(5);

        for (size_t objIdx = 0; objIdx < objects.size(); ++objIdx)
        {
            const auto& obj = objects[objIdx];
            if (obj.isNegative || obj.cpuVertices.empty() || obj.cpuIndices.size() < 3)
            {
                continue;
            }

            auto model = obj.getModelMatrix();

            // Transform vertices to world space and scale to millimeters (x 1000)
            std::map<std::tuple<int64_t, int64_t, int64_t>, uint32_t> coordMap;
            std::vector<uint32_t> remap(obj.cpuVertices.size());
            std::vector<utils::math::Vec3f> uniqueWorldPts;

            for (size_t vIdx = 0; vIdx < obj.cpuVertices.size(); ++vIdx)
            {
                const auto& v = obj.cpuVertices[vIdx];
                utils::math::Vec3f localPos{v.position[0], v.position[1], v.position[2]};
                auto worldPos = model * localPos;
                // Convert meters to millimeters
                float mmX = worldPos.x * 1000.0f;
                float mmY = worldPos.y * 1000.0f;
                float mmZ = worldPos.z * 1000.0f;

                int64_t qx = static_cast<int64_t>(std::round(mmX * 100.0f));
                int64_t qy = static_cast<int64_t>(std::round(mmY * 100.0f));
                int64_t qz = static_cast<int64_t>(std::round(mmZ * 100.0f));
                auto key = std::make_tuple(qx, qy, qz);

                auto it = coordMap.find(key);
                if (it == coordMap.end())
                {
                    uint32_t newIdx = static_cast<uint32_t>(uniqueWorldPts.size());
                    coordMap[key] = newIdx;
                    remap[vIdx] = newIdx;
                    uniqueWorldPts.push_back({mmX, mmY, mmZ});
                }
                else
                {
                    remap[vIdx] = it->second;
                }
            }

            if (uniqueWorldPts.empty())
                continue;

            // Output CARTESIAN_POINT entities for unique vertices
            std::vector<uint64_t> ptEntityIds(uniqueWorldPts.size());
            for (size_t i = 0; i < uniqueWorldPts.size(); ++i)
            {
                ptEntityIds[i] = nextId++;
                file << "#" << ptEntityIds[i] << " = CARTESIAN_POINT('',(" << uniqueWorldPts[i].x
                     << "," << uniqueWorldPts[i].y << "," << uniqueWorldPts[i].z << "));\n";
            }

            // Generate ADVANCED_FACE entities for triangles
            std::vector<uint64_t> faceIds;
            faceIds.reserve(obj.cpuIndices.size() / 3);

            for (size_t i = 0; i + 2 < obj.cpuIndices.size(); i += 3)
            {
                uint32_t i0 = remap[obj.cpuIndices[i + 0]];
                uint32_t i1 = remap[obj.cpuIndices[i + 1]];
                uint32_t i2 = remap[obj.cpuIndices[i + 2]];

                if (i0 == i1 || i1 == i2 || i2 == i0)
                    continue;

                const auto& p0 = uniqueWorldPts[i0];
                const auto& p1 = uniqueWorldPts[i1];
                const auto& p2 = uniqueWorldPts[i2];

                utils::math::Vec3f edge1 = p1 - p0;
                utils::math::Vec3f edge2 = p2 - p0;
                utils::math::Vec3f normal = edge1.cross(edge2);
                float normLen = normal.length();
                if (normLen < 1e-5f)
                    continue;
                normal = normal * (1.0f / normLen);

                // Reference X direction in plane
                float e1Len = edge1.length();
                utils::math::Vec3f refDir = (e1Len > 1e-5f) ? (edge1 * (1.0f / e1Len))
                                                            : utils::math::Vec3f(1.0f, 0.0f, 0.0f);

                // Plane origin at centroid
                utils::math::Vec3f centroid = (p0 + p1 + p2) * (1.0f / 3.0f);

                uint64_t idPtOrigin = nextId++;
                file << "#" << idPtOrigin << " = CARTESIAN_POINT('',(" << centroid.x << ","
                     << centroid.y << "," << centroid.z << "));\n";

                uint64_t idDirZ = nextId++;
                file << "#" << idDirZ << " = DIRECTION('',(" << normal.x << "," << normal.y << ","
                     << normal.z << "));\n";

                uint64_t idDirX = nextId++;
                file << "#" << idDirX << " = DIRECTION('',(" << refDir.x << "," << refDir.y << ","
                     << refDir.z << "));\n";

                uint64_t idAxis = nextId++;
                file << "#" << idAxis << " = AXIS2_PLACEMENT_3D('',#" << idPtOrigin << ",#"
                     << idDirZ << ",#" << idDirX << ");\n";

                uint64_t idPlane = nextId++;
                file << "#" << idPlane << " = PLANE('',#" << idAxis << ");\n";

                uint64_t idPolyLoop = nextId++;
                file << "#" << idPolyLoop << " = POLY_LOOP('',(#" << ptEntityIds[i0] << ",#"
                     << ptEntityIds[i1] << ",#" << ptEntityIds[i2] << "));\n";

                uint64_t idBound = nextId++;
                file << "#" << idBound << " = FACE_OUTER_BOUND('',#" << idPolyLoop << ",.T.);\n";

                uint64_t idFace = nextId++;
                file << "#" << idFace << " = ADVANCED_FACE('',(#" << idBound << "),#" << idPlane
                     << ",.T.);\n";

                faceIds.push_back(idFace);
            }

            if (faceIds.empty())
                continue;

            // Shell & MANIFOLD_SOLID_BREP
            uint64_t idShell = nextId++;
            file << "#" << idShell << " = CLOSED_SHELL('',(";
            for (size_t fIdx = 0; fIdx < faceIds.size(); ++fIdx)
            {
                file << "#" << faceIds[fIdx];
                if (fIdx + 1 < faceIds.size())
                    file << ",";
            }
            file << "));\n";

            std::string partName =
                obj.name.empty() ? ("Part_" + std::to_string(objIdx + 1)) : obj.name;
            uint64_t idSolid = nextId++;
            file << "#" << idSolid << " = MANIFOLD_SOLID_BREP('" << partName << "',#" << idShell
                 << ");\n";
            solidBrepIds.push_back(idSolid);

            // PBR Color presentation style
            float r = std::clamp(obj.materialColor.x, 0.0f, 1.0f);
            float g = std::clamp(obj.materialColor.y, 0.0f, 1.0f);
            float b = std::clamp(obj.materialColor.z, 0.0f, 1.0f);

            uint64_t idColour = nextId++;
            file << "#" << idColour << " = COLOUR_RGB('" << partName << "_Color'," << r << "," << g
                 << "," << b << ");\n";

            uint64_t idFillColour = nextId++;
            file << "#" << idFillColour << " = FILL_AREA_STYLE_COLOUR('',#" << idColour << ");\n";

            uint64_t idFillStyle = nextId++;
            file << "#" << idFillStyle << " = FILL_AREA_STYLE('',(#" << idFillColour << "));\n";

            uint64_t idSurfStyle = nextId++;
            file << "#" << idSurfStyle << " = SURFACE_STYLE_FILL_AREA(#" << idFillStyle << ");\n";

            uint64_t idSurfSide = nextId++;
            file << "#" << idSurfSide << " = SURFACE_SIDE_STYLE('',(#" << idSurfStyle << "));\n";

            uint64_t idSurfUsage = nextId++;
            file << "#" << idSurfUsage << " = SURFACE_STYLE_USAGE(.BOTH.,#" << idSurfSide << ");\n";

            uint64_t idPresStyle = nextId++;
            file << "#" << idPresStyle << " = PRESENTATION_STYLE_ASSIGNMENT((#" << idSurfUsage
                 << "));\n";

            uint64_t idStyledItem = nextId++;
            file << "#" << idStyledItem << " = STYLED_ITEM('color',(#" << idPresStyle << "),#"
                 << idSolid << ");\n";
        }

        if (solidBrepIds.empty())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "STEP export failed: no solid bodies generated");
            file.close();
            return false;
        }

        // Assembly shape representation
        uint64_t idShapeRep = nextId++;
        file << "#" << idShapeRep << " = ADVANCED_BREP_SHAPE_REPRESENTATION('" << modelName
             << "',(";
        for (size_t sIdx = 0; sIdx < solidBrepIds.size(); ++sIdx)
        {
            file << "#" << solidBrepIds[sIdx];
            if (sIdx + 1 < solidBrepIds.size())
                file << ",";
        }
        file << "),#" << idGeomContext << ");\n";

        uint64_t idShapeDefRep = nextId++;
        file << "#" << idShapeDefRep << " = SHAPE_DEFINITION_REPRESENTATION(#" << idProdDefShape
             << ",#" << idShapeRep << ");\n";

        file << "ENDSEC;\n";
        file << "END-ISO-10303-21;\n";
        file.close();

        __android_log_print(ANDROID_LOG_INFO, "MobileCAD",
                            "STEP export: %zu solid bodies successfully written to %s",
                            solidBrepIds.size(), filepath.c_str());
        return true;
    }
};

}  // namespace io
