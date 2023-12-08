#include "chunkmanager.hpp"

#include <osg/Material>
#include <osg/Texture2D>

#include <osgUtil/IncrementalCompileOperation>

#include <components/resource/objectcache.hpp>
#include <components/resource/scenemanager.hpp>

#include <components/sceneutil/lightmanager.hpp>

#include "compositemaprenderer.hpp"
#include "material.hpp"
#include "storage.hpp"
#include "terraindrawable.hpp"
#include "texturemanager.hpp"

#include <components\debug\debuglog.hpp>
#include <osg/BlendFunc>


namespace Terrain
{

    ChunkManager::ChunkManager(Storage* storage, Resource::SceneManager* sceneMgr, TextureManager* textureManager,
        CompositeMapRenderer* renderer, ESM::RefId worldspace, double expiryDelay)
        : GenericResourceManager<ChunkKey>(nullptr, expiryDelay)
        , QuadTreeWorld::ChunkManager(worldspace)
        , mStorage(storage)
        , mSceneManager(sceneMgr)
        , mTextureManager(textureManager)
        , mCompositeMapRenderer(renderer)
        , mNodeMask(0)
        , mCompositeMapSize(512)
        , mCompositeMapLevel(1.f)
        , mMaxCompGeometrySize(1.f)
    {
        mMultiPassRoot = new osg::StateSet;
        mMultiPassRoot->setRenderingHint(osg::StateSet::OPAQUE_BIN);
        osg::ref_ptr<osg::Material> material(new osg::Material);
        material->setColorMode(osg::Material::AMBIENT_AND_DIFFUSE);
        mMultiPassRoot->setAttributeAndModes(material, osg::StateAttribute::ON);
    }

    osg::ref_ptr<osg::Node> ChunkManager::getChunk(float size, const osg::Vec2f& center, unsigned char lod,
        unsigned int lodFlags, bool activeGrid, const osg::Vec3f& viewPoint, bool compile)
    {
        // Override lod with the vertexLodMod adjusted value.
        // TODO: maybe we can refactor this code by moving all vertexLodMod code into this class.
        lod = static_cast<unsigned char>(lodFlags >> (4 * 4));

        const ChunkKey key{ .mCenter = center, .mLod = lod, .mLodFlags = lodFlags };
        if (osg::ref_ptr<osg::Object> obj = mCache->getRefFromObjectCache(key))
            return static_cast<osg::Node*>(obj.get());

        const TerrainDrawable* templateGeometry = nullptr;
        const TemplateKey templateKey{ .mCenter = center, .mLod = lod };
        const auto pair = mCache->lowerBound(templateKey);
        if (pair.has_value() && templateKey == TemplateKey{ .mCenter = pair->first.mCenter, .mLod = pair->first.mLod })
            templateGeometry = static_cast<const TerrainDrawable*>(pair->second.get());

        osg::ref_ptr<osg::Node> node = createChunk(size, center, lod, lodFlags, compile, templateGeometry);
        mCache->addEntryToObjectCache(key, node.get());
        return node;
    }

    void ChunkManager::reportStats(unsigned int frameNumber, osg::Stats* stats) const
    {
        stats->setAttribute(frameNumber, "Terrain Chunk", mCache->getCacheSize());
    }

    void ChunkManager::clearCache()
    {
        GenericResourceManager<ChunkKey>::clearCache();

        mBufferCache.clearCache();
    }

    void ChunkManager::releaseGLObjects(osg::State* state)
    {
        GenericResourceManager<ChunkKey>::releaseGLObjects(state);
        mBufferCache.releaseGLObjects(state);
    }

    osg::ref_ptr<osg::Texture2D> ChunkManager::createCompositeMapRTT()
    {
        osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D;
        texture->setTextureWidth(mCompositeMapSize);
        texture->setTextureHeight(mCompositeMapSize);
        texture->setInternalFormat(GL_RGB);
        texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
        texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
        texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
        texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);

        return texture;
    }

    void ChunkManager::createCompositeMapGeometry(int lod, float chunkSize, const osg::Vec2f& chunkCenter,
        const osg::Vec4f& texCoords, CompositeMap& compositeMap)
    {
        if (chunkSize > mMaxCompGeometrySize)
        {
            createCompositeMapGeometry(lod, chunkSize / 2.f, chunkCenter + osg::Vec2f(chunkSize / 4.f, chunkSize / 4.f),
                osg::Vec4f(
                    texCoords.x() + texCoords.z() / 2.f, texCoords.y(), texCoords.z() / 2.f, texCoords.w() / 2.f),
                compositeMap);
            createCompositeMapGeometry(lod, chunkSize / 2.f,
                chunkCenter + osg::Vec2f(-chunkSize / 4.f, chunkSize / 4.f),
                osg::Vec4f(texCoords.x(), texCoords.y(), texCoords.z() / 2.f, texCoords.w() / 2.f), compositeMap);
            createCompositeMapGeometry(lod, chunkSize / 2.f,
                chunkCenter + osg::Vec2f(chunkSize / 4.f, -chunkSize / 4.f),
                osg::Vec4f(texCoords.x() + texCoords.z() / 2.f, texCoords.y() + texCoords.w() / 2.f,
                    texCoords.z() / 2.f, texCoords.w() / 2.f),
                compositeMap);
            createCompositeMapGeometry(lod, chunkSize / 2.f,
                chunkCenter + osg::Vec2f(-chunkSize / 4.f, -chunkSize / 4.f),
                osg::Vec4f(
                    texCoords.x(), texCoords.y() + texCoords.w() / 2.f, texCoords.z() / 2.f, texCoords.w() / 2.f),
                compositeMap);
        }
        else
        {
            //float left = texCoords.x() * 2.f - 1;
            //float top = texCoords.y() * 2.f - 1;
            //float width = texCoords.z() * 2.f;
            //float height = texCoords.w() * 2.f;

            float posX = texCoords.x() * 2.f - 1 + texCoords.z();
            float posY = texCoords.y() * 2.f - 1 + texCoords.w();
            float width = texCoords.z() * 2.f;
            float height = texCoords.w() * -2.f;

            int compositeMapLod = 0; //lod > 1 ? 1 : 0;

            osg::Vec4 geomKey = osg::Vec4(chunkSize, posX, posY, width);

            osg::ref_ptr<osg::Geometry> templateGeom;
            std::map<osg::Vec4f, osg::ref_ptr<osg::Geometry>>::iterator found
                = mCompositeMapGeometryCache.find(geomKey);
            if (found != mCompositeMapGeometryCache.end())
            {
                templateGeom = found->second;
            }
            else
            {
                Log(Debug::Info) << "CREATING TEMPLATE GEOM " << texCoords.x() << " " << texCoords.y() << " "
                                 << texCoords.z() << " " << texCoords.w();

                unsigned int numVerts = ((mStorage->getCellVertices(mWorldspace) - 1) >> compositeMapLod) * chunkSize + 1;
                
                //const std::size_t sampleSize = std::size_t{ 1 } << compositeMapLod;
                //const std::size_t cellSize = static_cast<std::size_t>(ESM::getLandSize(mWorldspace));
                //const std::size_t numVerts = static_cast<std::size_t>(chunkSize * (cellSize - 1) / sampleSize) + 1;

                osg::ref_ptr<osg::Vec3Array> positions = new osg::Vec3Array(numVerts * numVerts);
                for (int vertY = 0; vertY < numVerts; vertY++)
                {
                    for (int vertX = 0; vertX < numVerts; vertX++)
                    {
                        (*positions)[vertY + vertX * numVerts]
                            = osg::Vec3f((vertX / static_cast<float>(numVerts - 1) - 0.5f) * width + posX,
                                (vertY / static_cast<float>(numVerts - 1) - 0.5f) * height + posY, 0);
                    }
                }
                //osg::ref_ptr<osg::Vec4ubArray> colors(new osg::Vec4ubArray);

                //osg::ref_ptr<osg::Vec4ubArray> colors = new osg::Vec4ubArray(1);
                //(*colors)[0].set(255, 255, 255, 255);

                osg::ref_ptr<osg::Vec2Array> uvs = mBufferCache.getUVBuffer(numVerts);
                osg::ref_ptr<osg::DrawElements> tris = mBufferCache.getIndexBuffer(numVerts, 0);


                templateGeom = new osg::Geometry;
                templateGeom->setVertexArray(positions);
                //templateGeom->setColorArray(colors, osg::Array::BIND_OVERALL);
                templateGeom->setTexCoordArray(0, uvs);
                templateGeom->addPrimitiveSet(tris);

                mCompositeMapGeometryCache[geomKey] = templateGeom;
            }


            float left = texCoords.x() * 2.f - 1;
            float top = texCoords.y() * 2.f - 1;
            float width2 = texCoords.z() * 2.f;
            float height2 = texCoords.w() * 2.f;


            osg::ref_ptr<osg::Vec4ubArray> colors = new osg::Vec4ubArray();
            mStorage->fillVertexBuffersCompositeMap(compositeMapLod, chunkSize, chunkCenter, mWorldspace, *colors);

            std::vector<osg::ref_ptr<osg::StateSet>> passes = createPasses(chunkSize, chunkCenter, true);
            for (std::vector<osg::ref_ptr<osg::StateSet>>::iterator it = passes.begin(); it != passes.end(); ++it)
            {
                //auto corner = osg::Vec3(left, top, 0);
                //auto widthVec = osg::Vec3(width, 0, 0);
                //auto heightVec = osg::Vec3(0, height, 0);

               
                osg::ref_ptr<osg::Geometry> geom = osg::createTexturedQuadGeometry(
                osg::Vec3(left, top, 0), osg::Vec3(width2, 0, 0), osg::Vec3(0, height2, 0));
               
                //osg::Vec3Array* coords = new osg::Vec3Array(numVerts * numVerts);

                /*
                osg::Vec3Array* coords = new osg::Vec3Array(4);
                (*coords)[0] = corner + heightVec;
                (*coords)[1] = corner;
                (*coords)[2] = corner + widthVec;
                (*coords)[3] = corner + widthVec + heightVec;

                //(*tcoords)[0].set(0, 1);
                //(*tcoords)[1].set(0, 0);
                //(*tcoords)[2].set(1, 0);
                //(*tcoords)[3].set(1, 1);

                osg::Vec4Array* colours = new osg::Vec4Array(1);
                (*colours)[0].set(1.0f, 0.0f, 0.0, 1.0f);
                geom->setColorArray(colours, osg::Array::BIND_OVERALL);

                osg::Vec3Array* normals = new osg::Vec3Array(1);
                (*normals)[0] = widthVec ^ heightVec;
                (*normals)[0].normalize();
                geom->setNormalArray(normals, osg::Array::BIND_OVERALL);

                osg::DrawElementsUByte* elems = new osg::DrawElementsUByte(osg::PrimitiveSet::TRIANGLES);
                elems->push_back(0);
                elems->push_back(1);
                elems->push_back(2);

                elems->push_back(2);
                elems->push_back(3);
                elems->push_back(0);
                geom->addPrimitiveSet(elems);
                */


                geom->setUseDisplayList(
                    false); // don't bother making a display list for an object that is just rendered once.
                geom->setUseVertexBufferObjects(false);
                geom->setTexCoordArray(1, geom->getTexCoordArray(0), osg::Array::BIND_PER_VERTEX);

                geom->setStateSet(*it);

                compositeMap.mDrawables.emplace_back(geom);
            }

            {
                
                osg::ref_ptr<osg::Geometry> geom = new osg::Geometry();
                geom->setVertexArray(templateGeom->getVertexArray());
                geom->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
                geom->addPrimitiveSet(templateGeom->getPrimitiveSet(0));
                
                //osg::ref_ptr<osg::Geometry> geom = osg::createTexturedQuadGeometry(
                //    osg::Vec3(left, top, 0), osg::Vec3(width2 / 2, 0, 0), osg::Vec3(0, height2 / 2, 0));

                //osg::Vec4Array* colours2 = new osg::Vec4Array(1);
                //(*colours2)[0].set(1.0f, 0.0f, 0.0, 1.0f);
                //geom->setColorArray(colours2, osg::Array::BIND_OVERALL);


                geom->setUseDisplayList(false);
                geom->setUseVertexBufferObjects(false);
                //geom->setTexCoordArray(1, geom->getTexCoordArray(0), osg::Array::BIND_PER_VERTEX);

                osg::ref_ptr<osg::StateSet> stateset(new osg::StateSet);
                stateset->setMode(GL_BLEND, osg::StateAttribute::ON);
                stateset->setAttributeAndModes(new osg::BlendFunc(osg::BlendFunc::ZERO, osg::BlendFunc::SRC_COLOR), osg::StateAttribute::ON);

                geom->setStateSet(stateset);

                compositeMap.mDrawables.emplace_back(geom);

            }
        }

        
        
    }

    std::vector<osg::ref_ptr<osg::StateSet>> ChunkManager::createPasses(
        float chunkSize, const osg::Vec2f& chunkCenter, bool forCompositeMap)
    {
        std::vector<LayerInfo> layerList;
        std::vector<osg::ref_ptr<osg::Image>> blendmaps;
        mStorage->getBlendmaps(chunkSize, chunkCenter, blendmaps, layerList, mWorldspace);

        bool useShaders = mSceneManager->getForceShaders();
        if (!mSceneManager->getClampLighting())
            useShaders = true; // always use shaders when lighting is unclamped, this is to avoid lighting seams between
                               // a terrain chunk with normal maps and one without normal maps

        std::vector<TextureLayer> layers;
        {
            for (std::vector<LayerInfo>::const_iterator it = layerList.begin(); it != layerList.end(); ++it)
            {
                TextureLayer textureLayer;
                textureLayer.mParallax = it->mParallax;
                textureLayer.mSpecular = it->mSpecular;

                textureLayer.mDiffuseMap = mTextureManager->getTexture(it->mDiffuseMap);

                if (!forCompositeMap && !it->mNormalMap.empty())
                    textureLayer.mNormalMap = mTextureManager->getTexture(it->mNormalMap);

                if (it->requiresShaders())
                    useShaders = true;

                layers.push_back(textureLayer);
            }
        }

        if (forCompositeMap)
            useShaders = false;

        std::vector<osg::ref_ptr<osg::Texture2D>> blendmapTextures;
        for (std::vector<osg::ref_ptr<osg::Image>>::const_iterator it = blendmaps.begin(); it != blendmaps.end(); ++it)
        {
            osg::ref_ptr<osg::Texture2D> texture(new osg::Texture2D);
            texture->setImage(*it);
            texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            texture->setResizeNonPowerOfTwoHint(false);
            blendmapTextures.push_back(texture);
        }

        float blendmapScale = mStorage->getBlendmapScale(chunkSize);

        return ::Terrain::createPasses(
            useShaders, mSceneManager, layers, blendmapTextures, blendmapScale, blendmapScale);
    }

    osg::ref_ptr<osg::Node> ChunkManager::createChunk(float chunkSize, const osg::Vec2f& chunkCenter, unsigned char lod,
        unsigned int lodFlags, bool compile, const TerrainDrawable* templateGeometry)
    {
        osg::ref_ptr<TerrainDrawable> geometry(new TerrainDrawable);

        bool useCompositeMap = chunkSize >= mCompositeMapLevel;
        unsigned int numUvSets = useCompositeMap ? 1 : 2;


        if (!templateGeometry)
        {
            osg::ref_ptr<osg::Vec3Array> positions(new osg::Vec3Array);
            osg::ref_ptr<osg::Vec3Array> normals(new osg::Vec3Array);
            osg::ref_ptr<osg::Vec4ubArray> colors(new osg::Vec4ubArray);
            colors->setNormalize(true);

            mStorage->fillVertexBuffers(lod, chunkSize, chunkCenter, mWorldspace, *positions, *normals, *colors, useCompositeMap);

            osg::ref_ptr<osg::VertexBufferObject> vbo(new osg::VertexBufferObject);
            positions->setVertexBufferObject(vbo);
            normals->setVertexBufferObject(vbo);
            colors->setVertexBufferObject(vbo);

            geometry->setVertexArray(positions);
            geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
            geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
        }
        else
        {
            // Unfortunately we need to copy vertex data because of poor coupling with VertexBufferObject.
            osg::ref_ptr<osg::Array> positions
                = static_cast<osg::Array*>(templateGeometry->getVertexArray()->clone(osg::CopyOp::DEEP_COPY_ALL));
            osg::ref_ptr<osg::Array> normals
                = static_cast<osg::Array*>(templateGeometry->getNormalArray()->clone(osg::CopyOp::DEEP_COPY_ALL));
            osg::ref_ptr<osg::Array> colors
                = static_cast<osg::Array*>(templateGeometry->getColorArray()->clone(osg::CopyOp::DEEP_COPY_ALL));

            osg::ref_ptr<osg::VertexBufferObject> vbo(new osg::VertexBufferObject);
            positions->setVertexBufferObject(vbo);
            normals->setVertexBufferObject(vbo);
            colors->setVertexBufferObject(vbo);

            geometry->setVertexArray(positions);
            geometry->setNormalArray(normals, osg::Array::BIND_PER_VERTEX);
            geometry->setColorArray(colors, osg::Array::BIND_PER_VERTEX);
        }

        geometry->setUseDisplayList(false);
        geometry->setUseVertexBufferObjects(true);

        if (chunkSize <= 1.f)
            geometry->setLightListCallback(new SceneUtil::LightListCallback);

        unsigned int numVerts = (mStorage->getCellVertices(mWorldspace) - 1) * chunkSize / (1 << lod) + 1;

        geometry->addPrimitiveSet(mBufferCache.getIndexBuffer(numVerts, lodFlags));


        geometry->setTexCoordArrayList(osg::Geometry::ArrayList(numUvSets, mBufferCache.getUVBuffer(numVerts)));

        geometry->createClusterCullingCallback();

        geometry->setStateSet(mMultiPassRoot);

        if (templateGeometry)
        {
            if (templateGeometry->getCompositeMap())
            {
                geometry->setCompositeMap(templateGeometry->getCompositeMap());
                geometry->setCompositeMapRenderer(mCompositeMapRenderer);
            }
            geometry->setPasses(templateGeometry->getPasses());
        }
        else
        {
            if (useCompositeMap)
            {
                osg::ref_ptr<CompositeMap> compositeMap = new CompositeMap;
                compositeMap->mTexture = createCompositeMapRTT();

                createCompositeMapGeometry(lod, chunkSize, chunkCenter, osg::Vec4f(0, 0, 1, 1), *compositeMap);

                mCompositeMapRenderer->addCompositeMap(compositeMap.get(), false);

                geometry->setCompositeMap(compositeMap);
                geometry->setCompositeMapRenderer(mCompositeMapRenderer);

                TextureLayer layer;
                layer.mDiffuseMap = compositeMap->mTexture;
                layer.mParallax = false;
                layer.mSpecular = false;
                geometry->setPasses(::Terrain::createPasses(
                    mSceneManager->getForceShaders() || !mSceneManager->getClampLighting(), mSceneManager,
                    std::vector<TextureLayer>(1, layer), std::vector<osg::ref_ptr<osg::Texture2D>>(), 1.f, 1.f));
            }
            else
            {
                geometry->setPasses(createPasses(chunkSize, chunkCenter, false));
            }
        }

        geometry->setupWaterBoundingBox(-1, chunkSize * mStorage->getCellWorldSize(mWorldspace) / numVerts);

        if (!templateGeometry && compile && mSceneManager->getIncrementalCompileOperation())
        {
            mSceneManager->getIncrementalCompileOperation()->add(geometry);
        }
        geometry->setNodeMask(mNodeMask);

        return geometry;
    }

}
