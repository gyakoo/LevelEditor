#pragma once

namespace champ
{
    struct Model3D;
    struct MtlParser;
    struct v3 { float x, y, z; operator float*() { return &x; } };
    struct v2 { float x, y; operator float*() { return &x; } };
    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    struct ObjParser
    {
        struct Vertex
        {
            v3 pos;
            v3 normal;
            v2 uv;
        };

        struct SubObj
        {
#ifdef FORCE_16BIT_INDICES
            typedef uint16_t IndexType;
#else
            typedef uint32_t IndexType;
#endif
            std::vector<Vertex> m_vertexBuffer;
            std::vector<IndexType> m_indexBuffer;
            std::string m_name;
            std::string m_material;
        };

    public:
        ObjParser(const std::vector<byte>& data);

        std::vector<SubObj> m_objects;        
        std::string m_materialLib;
    };

    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////////////////////////////
    struct MtlParser
    {
        // http://paulbourke.net/dataformats/mtl/
        struct Material
        {
            std::string name;     // mtl name
            std::string map_ka;   // map ambient tex
            std::string map_ks;   // map specular tex
            std::string map_kd;   // map diffuse tex
            std::string map_d;    // map alpha texture
            std::string map_ns;   // map specular hightlight component
            std::string map_bump; // map bump mapping
            std::string map_disp; // map displacement mapping
            std::string map_decal;// map stencil decal
            v3    ka;     // ambient color
            v3    kd;     // diffuse color
            v3    ks;     // specular color
            v3    tf;     // transmission filter
            float       tr;     // transparancy
            float       ns;     // specular hightlight 
            float       sharpness; // sharpness of the reflection
            float       ni;     // index of refraction
            uint8_t     illum;  // illumination mode (see link) 0..10 
        };

    public:
        void Parse(const std::vector<byte>& data);

        std::map<std::string, Material*> m_materials;
    };
};