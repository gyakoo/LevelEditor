#include "../Core/Utils.h"
#include "../Core/Logger.h"
#include "../Core/FileUtils.h"
#include "../ResourceManager/ResourceManager.h"
#include "Model3dBuilder.h"
#include "ObjModelFactory.h"
#include <algorithm>
#include <functional> 
#include <cctype>

namespace LvEdEngine
{
    struct ObjParser
    {
        struct Vertex
        {
            float3 pos;
            float3 normal;
            float2 uv;
        };

        struct SubObj
        {
            std::vector<Vertex> m_vertexBuffer;
#ifdef FORCE_16BIT_INDICES
            std::vector<uint16_t> m_indexBuffer;
#else
            std::vector<uint32_t> m_indexBuffer;
#endif
            std::string m_name;
            std::string m_material;
        };

        ObjParser(const std::vector<byte>& data);

        std::vector<SubObj> m_objects;
        std::string m_materialLib;
    };

    struct Group
    {
        size_t m_startFace;
        size_t m_faceCount;
        std::string m_usemtl;
    };

    static inline bool IsBlank(char c)
    {
        return c == ' ' || c == '\t' || c == '\r';
    }

    static inline size_t FindNextBlank(const std::string& str, size_t ndx)
    {
        size_t i = ndx;
        while (i < str.length() && !IsBlank(str[i]))
            ++i;
        return i;
    }

    static inline size_t ConsumeBlanks(const std::string& str, size_t ndx)
    {
        size_t i = ndx;
        while (i < str.length() && IsBlank(str[i]))
            ++i;
        return i;
    }

    template<int N, typename V>
    inline void ParseFloats(const std::string& str, V& out, size_t idx)
    {
        std::string subs;
        size_t lastIdx = std::string::npos;
        float* xyz = &out.x;
        for (int i = 0; i < N; ++i)
        {
            idx = ConsumeBlanks(str, idx);
            if (idx >= str.length()) break;
            lastIdx = idx;
            idx = FindNextBlank(str, idx);
            if (lastIdx == idx) break;
            subs = str.substr(lastIdx, idx - lastIdx);
            xyz[i] = (float)::atof(subs.c_str());
        }
    }

    inline void ParseInts3(std::string& str, int32_t* outValues)
    {
        size_t i[3];
        i[0] = 0;
        size_t _i = 1;
        size_t count = str.length();
        for (size_t j = 0; j < count; ++j)
        {
            if (str[j] == '/')
            {
                str[j] = 0;
                i[_i++] = j + 1;
            }
        }
        outValues[0] = ::atoi(str.c_str()) - 1;
        outValues[1] = ::atoi(str.c_str() + i[1]) - 1;
        outValues[2] = ::atoi(str.c_str() + i[2]) - 1;
    }

    template<typename T3, typename T2>
    static inline bool ParseFace(const std::string& line, size_t idx, const T3& pos, const T3& nor, const T2& uv, std::vector<ObjParser::Vertex>& outVertices)
    {
        ObjParser::Vertex v;
        int32_t indices[3];     // v, vt, vn
        size_t lastNdx;
        size_t count = 3;
        for (int i = 0; i < count; ++i) // at least three vertices
        {
            idx = ConsumeBlanks(line, idx);
            if (idx >= line.length()) break;
            lastNdx = idx;
            idx = FindNextBlank(line, idx);
            std::string subs = line.substr(lastNdx, idx - lastNdx);
            ParseInts3(subs, indices);
            if (indices[0] < pos.size() && indices[2] < nor.size())
            {
                v.pos = pos[indices[0]];
                if (indices[1] >= 0 && indices[1] < uv.size())
                    v.uv = uv[indices[1]];
                else
                    v.uv.x = v.uv.y = 0.0f;
                v.normal = nor[indices[2]];
                if (i == 3) // it's a quad, triangulate fan
                {
                    if (outVertices.size() < 3)
                        break; // some index was wrong

                    outVertices.push_back(outVertices[0]);
                    outVertices.push_back(outVertices[2]);
                }
                outVertices.push_back(v);
            }
            if (i >= 2 && idx < line.length())
                ++count;
        }
        const size_t cv = outVertices.size();
        return cv == 3 || cv == 6;
    }


    // trim from start
    inline std::string &StrLtrim(std::string &s)
    {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
    }

    // trim from end
    inline std::string &StrRtrim(std::string &s)
    {
        s.erase(std::find_if(s.rbegin(), s.rend(),
            std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
    }

    // trim from both ends
    inline std::string &StrTrim(std::string &s)
    {
        return StrLtrim(StrRtrim(s));
    }

    inline bool GetLine(const std::vector<byte>& data, size_t& cur, std::string& outLine)
    {
        if (cur >= data.size()) return false;
        outLine.clear();
        char c;
        while (cur < data.size() && (c = data[cur]) != '\n')
        {
            outLine += c;
            ++cur;
        }

        if (c == '\n')
            ++cur;

        StrTrim(outLine);
        return cur <= data.size();
    }

    ObjParser::ObjParser(const std::vector<byte>& data)
    {
        std::vector<Group> groups;
        std::vector<float3> positions;
        std::vector<float3> normals;
        std::vector<float2> texCoords;
        std::vector<Vertex> tempVertices;
        tempVertices.reserve(6);
        size_t cur = 0;
        std::string line;
        line.reserve(512);
        float3 _float3;
        float2 _float2;
        bool doneRecenter = false;
        while (GetLine(data, cur, line))
        {
            if (line.empty()) continue;
            switch (*line.begin())
            {
            case '#':
                if (line.find_first_of("NORECENTER", 0) != std::string::npos)
                    doneRecenter = true;
                break;
            case 'v':
                switch (*(line.begin() + 1))
                {
                case ' ':
                    ParseFloats<3, float3>(line, _float3, 2);
                    positions.push_back(_float3);
                    break;
                case 'n':
                    ParseFloats<3, float3>(line, _float3, 3);
                    normals.push_back(_float3);
                    break;
                case 't':
                    ParseFloats<2, float2>(line, _float2, 3);
                    _float2.y = 1.0f - _float2.y;
                    texCoords.push_back(_float2);
                    break;
                }
                break;

            case 'm':
                m_materialLib = line.substr(7);
                break;

            case 'u':
                m_objects.back().m_material = line.substr(7);
                break;

            case 'f':
            {
                // recenter
                if (!doneRecenter)
                {
                    if (!positions.empty())
                    {
                        float3 com(0, 0, 0);
                        for (const auto& p : positions) com += p;
                        com *= 1.0f / positions.size();

                        for (auto& p : positions)
                            p = p - com;
                    }
                    doneRecenter = true;
                }

                tempVertices.clear();
                if (ParseFace(line, 2, positions, normals, texCoords, tempVertices))
                {
                    auto& v = m_objects.back().m_vertexBuffer;
                    v.insert(v.end(), tempVertices.begin(), tempVertices.end());
                }
            }break;

            case 'g':
            case 'o':
            {
                SubObj obj;
                obj.m_name = line.substr(2);
                m_objects.push_back(obj);
            }break;
            }
        }

        // prepare IBs (very simple, it's an expanded triangle soup)
        for (auto& o : m_objects)
        {
#if defined(_DEBUG) && defined(FORCE_16BIT_INDICES)
            if (o.m_vertexBuffer.size() >= (1 << 16))
                throw Platform::Exception::CreateException(E_FAIL, L"This OBJ needs 32 bits IB. Only 16 bits supported. Will be truncated in RELEASE");
#endif

#ifdef FORCE_16BIT_INDICES
            const size_t maxVerts = std::min(size_t((1 << 16) - 1), o.m_vertexBuffer.size());
#else
            const size_t maxVerts = o.m_vertexBuffer.size();
#endif
            auto& ib = o.m_indexBuffer;
            ib.reserve(maxVerts);
            for (size_t i = 0; i < maxVerts; ++i)
                ib.push_back(i);
        }
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

            ObjParser parser(vData);
            
            
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