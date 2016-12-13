#include "../Core/Utils.h"
#include "../Core/Logger.h"
#include "../Core/FileUtils.h"
#include "../ResourceManager/ResourceManager.h"
#include "Model3dBuilder.h"
#include "ObjModelFactory.h"
#include "ObjParser.h"
#include <locale>
#include <codecvt>

namespace LvEdEngine
{

    inline std::wstring ToWstring(const std::string& str)
    {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.from_bytes(str);
    }

    //
    // Gets the full path without the last element
    //
    inline std::wstring StrExtractPath(const std::wstring& str)
    {
        auto p = str.find_last_of(L'/');
        if (p == std::wstring::npos)
        {
            p = str.find_last_of(L'\\');
            if (p == std::wstring::npos)
                return str;
        }
        return str.substr(0, p);
    }

    // 
    // Gets the filename from path
    // 
    template<typename STR>
    inline STR StrExtractFilename(const STR& str)
    {
        auto p = str.find_last_of(STR::value_type('/'));
        if (p == STR::npos)
        {
            p = str.find_last_of(STR::value_type('\\'));
            if (p == std::wstring::npos)
                return str;
        }

        return str.substr(p + 1);
    }

    ObjModelFactory::ObjModelFactory(ID3D11Device* device)
        : m_device(device)
    {
    }
    
    bool ObjModelFactory::LoadResource(Resource* resource, const WCHAR * filename)
    {
        UINT dataSize;
        BYTE* data = FileUtils::LoadFile(filename, &dataSize);
        if (!data)
        {
            return false;
        }

        Model * model = (Model*)resource;
        model->SetSourceFileName(filename);

        Model3dBuilder builder;
        builder.m_model = model;

        bool succeeded = true;
        builder.Begin();
        {
            std::vector<byte> vData; vData.resize(dataSize);
            memcpy(&vData[0], data, dataSize);
            SAFE_DELETE_ARRAY(data);

            // let's parse OBJ
            champ::ObjParser parser(vData);
            champ::MtlParser mtlParser;
            if (!parser.m_materialLib.empty())
            {
                // let's parse Mtl
                std::wstring matFname = ToWstring(parser.m_materialLib);
                BYTE* matData = FileUtils::LoadFile(matFname.c_str(), &dataSize);
                if (!matData)
                {
                    matFname = StrExtractPath(filename) + L"\\" + StrExtractFilename(matFname);
                    matData = FileUtils::LoadFile(matFname.c_str(), &dataSize);
                }

                if (matData && dataSize>0)
                {
                    std::vector<byte> matVData; matVData.resize(dataSize);
                    memcpy(&matVData[0], matData, dataSize);
                    SAFE_DELETE_ARRAY(matData);
                    mtlParser.Parse(matVData);
                }
            }

            // materials
            for (auto m : mtlParser.m_materials)
            {
                Material* material = builder.m_model->CreateMaterial(m.first);
                champ::MtlParser::Material* mat = m.second;

                material->ambient = float4(mat->ka);
                material->diffuse = float4(mat->kd);
                material->specular = float4(mat->ks);
                material->power = mat->ns;
                material->texNames[TextureType::DIFFUSE] = StrExtractFilename(mat->map_kd);
                material->texNames[TextureType::SPEC] = StrExtractFilename(mat->map_ks);
            }

            // scene
            {
                Node* sceneNode = builder.m_model->CreateNode("OBJSceneNode");
                builder.m_model->SetRoot(sceneNode);

                // Add all objects to scenenode
                for (auto& o : parser.m_objects)
                {
                    Node* node = builder.m_model->CreateNode(o.m_name.c_str());
                    node->parent = sceneNode;
                    node->transform.MakeIdentity();
                    sceneNode->children.push_back(node);

                    builder.Mesh_Reset();
                    // source
                    {
                        auto& src = builder.m_mesh.source;
                        src.pos.reserve(o.m_vertexBuffer.size());
                        src.nor.reserve(o.m_vertexBuffer.size());
                        src.tex.reserve(o.m_vertexBuffer.size());
                        for (auto& v : o.m_vertexBuffer)
                        {
                            src.pos.push_back(float3(v.pos));
                            src.nor.push_back(float3(v.normal));
                            src.tex.push_back(float2(v.uv));
                        }
                    }

                    // primitives
                    {
                        Geometry* geo = builder.m_model->CreateGeometry(o.m_name + "_geo");
                        geo->material = builder.m_model->GetMaterial(o.m_material);
                        node->geometries.push_back(geo);
                        builder.Mesh_Begin(geo->name.c_str());
                        auto& bmesh = builder.m_mesh;
                        builder.Mesh_SetPrimType("TRIANGLES");
                        auto& polyInfo = bmesh.poly;
                        UINT stride = 0;
                        builder.Mesh_ResetPolyInfo();
                        polyInfo.hasNor = polyInfo.hasPos = polyInfo.hasTex = true;
                        polyInfo.posOffset = 0;
                        polyInfo.norOffset = 0;
                        polyInfo.texOffset = 0;
                        polyInfo.stride = 1;
                        const size_t polycount = o.m_indexBuffer.size() / 3;
                        polyInfo.vcount.reserve(polycount);
                        for (size_t i = 0; i < polycount; ++i)
                            polyInfo.vcount.push_back(3); // 3 vertices per poly
                        for (size_t i = 0; i < o.m_indexBuffer.size(); ++i)
                            polyInfo.indices.push_back(o.m_indexBuffer[i]); // indices
                        builder.Mesh_AddPolys();
                        geo->mesh = bmesh.mesh;
                        builder.Mesh_End();
                    }

                }
            }

        }
        builder.End();
        if (succeeded)
            model->Construct(m_device, ResourceManager::Inst());
        else
            model->Destroy();
    }
    
    Resource* ObjModelFactory::CreateResource(Resource* )
    {
        return new Model();
    }
}