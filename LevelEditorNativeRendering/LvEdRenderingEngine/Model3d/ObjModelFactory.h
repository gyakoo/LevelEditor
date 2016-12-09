#pragma once

namespace LvEdEngine
{

    class Model3dBuilder;

    //--------------------------------------------------
    class ObjModelFactory : public ResourceFactory
    {
    public:
        ObjModelFactory(ID3D11Device* device);
        virtual bool LoadResource(Resource* resource, const WCHAR * filename);
        virtual Resource* CreateResource(Resource* def);

    protected:
        ID3D11Device* m_device;
    };
};
