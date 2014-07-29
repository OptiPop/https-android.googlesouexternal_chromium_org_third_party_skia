/*
* Copyright 2014 Google Inc.
*
* Use of this source code is governed by a BSD-style license that can be
* found in the LICENSE file.
*/

#if SK_SUPPORT_GPU

#include "GrContext.h"
#include "GrContextFactory.h"
#include "GrLayerCache.h"
#include "SkPictureRecorder.h"
#include "Test.h"

static const int kNumLayers = 5;

class TestingAccess {
public:
    static int NumLayers(GrLayerCache* cache) {
        return cache->numLayers();
    }
    static void Purge(GrLayerCache* cache, uint32_t pictureID) {
        cache->purge(pictureID);
    }
};

// Add several layers to the cache
static void create_layers(skiatest::Reporter* reporter,
                          GrLayerCache* cache,
                          const SkPicture& picture) {
    GrCachedLayer* layers[kNumLayers];

    for (int i = 0; i < kNumLayers; ++i) {
        layers[i] = cache->findLayerOrCreate(&picture, i);
        REPORTER_ASSERT(reporter, NULL != layers[i]);
        GrCachedLayer* layer = cache->findLayer(&picture, i);
        REPORTER_ASSERT(reporter, layer == layers[i]);

        REPORTER_ASSERT(reporter, TestingAccess::NumLayers(cache) == i + 1);

        REPORTER_ASSERT(reporter, picture.uniqueID() == layers[i]->pictureID());
        REPORTER_ASSERT(reporter, layers[i]->layerID() == i);
        REPORTER_ASSERT(reporter, NULL == layers[i]->texture());
        REPORTER_ASSERT(reporter, !layers[i]->isAtlased());
    }

    cache->trackPicture(&picture);
}

// This test case exercises the public API of the GrLayerCache class.
// In particular it checks its interaction with the resource cache (w.r.t.
// locking & unlocking textures).
// TODO: need to add checks on VRAM usage!
DEF_GPUTEST(GpuLayerCache, reporter, factory) {
    for (int i= 0; i < GrContextFactory::kGLContextTypeCnt; ++i) {
        GrContextFactory::GLContextType glCtxType = (GrContextFactory::GLContextType) i;

        if (!GrContextFactory::IsRenderingGLContext(glCtxType)) {
            continue;
        }

        GrContext* context = factory->get(glCtxType);

        if (NULL == context) {
            continue;
        }

        SkPictureRecorder recorder;
        recorder.beginRecording(1, 1);
        SkAutoTUnref<const SkPicture> picture(recorder.endRecording());

        GrLayerCache cache(context);

        create_layers(reporter, &cache, *picture);

        // Lock the layers making them all 512x512
        GrTextureDesc desc;
        desc.fWidth = 512;
        desc.fHeight = 512;
        desc.fConfig = kSkia8888_GrPixelConfig;

        for (int i = 0; i < kNumLayers; ++i) {
            GrCachedLayer* layer = cache.findLayer(picture, i);
            REPORTER_ASSERT(reporter, NULL != layer);

            bool foundInCache = cache.lock(layer, desc);
            REPORTER_ASSERT(reporter, !foundInCache);
            foundInCache = cache.lock(layer, desc);
            REPORTER_ASSERT(reporter, foundInCache);

            REPORTER_ASSERT(reporter, NULL != layer->texture());
#if USE_ATLAS
            // The first 4 layers should be in the atlas (and thus have non-empty
            // rects)
            if (i < 4) {
                REPORTER_ASSERT(reporter, layer->isAtlased());
            } else {
#endif
            REPORTER_ASSERT(reporter, !layer->isAtlased());
#if USE_ATLAS
            }
#endif
        }

        // Unlock the textures
        for (int i = 0; i < kNumLayers; ++i) {
            GrCachedLayer* layer = cache.findLayer(picture, i);
            REPORTER_ASSERT(reporter, NULL != layer);

            cache.unlock(layer);
        }

        for (int i = 0; i < kNumLayers; ++i) {
            GrCachedLayer* layer = cache.findLayer(picture, i);
            REPORTER_ASSERT(reporter, NULL != layer);

#if USE_ATLAS
            // The first 4 layers should be in the atlas (and thus do not 
            // currently unlock). The final layer should be unlocked.
            if (i < 4) {
                REPORTER_ASSERT(reporter, NULL != layer->texture());
                REPORTER_ASSERT(reporter, layer->isAtlased());
            } else {
#endif
                REPORTER_ASSERT(reporter, NULL == layer->texture());
                REPORTER_ASSERT(reporter, !layer->isAtlased());
#if USE_ATLAS
            }
#endif
        }

        //--------------------------------------------------------------------
        // Free them all SkGpuDevice-style. This will not free up the
        // atlas' texture but will eliminate all the layers.
        TestingAccess::Purge(&cache, picture->uniqueID());

        REPORTER_ASSERT(reporter, TestingAccess::NumLayers(&cache) == 0);
        // TODO: add VRAM/resource cache check here

        //--------------------------------------------------------------------
        // Test out the GrContext-style purge. This should remove all the layers
        // and the atlas.
        // Re-create the layers
        create_layers(reporter, &cache, *picture);

        // Free them again GrContext-style. This should free up everything.
        cache.freeAll();

        REPORTER_ASSERT(reporter, TestingAccess::NumLayers(&cache) == 0);
        // TODO: add VRAM/resource cache check here

        //--------------------------------------------------------------------
        // Test out the MessageBus-style purge. This will not free the atlas
        // but should eliminate the free-floating layers.
        create_layers(reporter, &cache, *picture);

        picture.reset(NULL);
        cache.processDeletedPictures();

        REPORTER_ASSERT(reporter, TestingAccess::NumLayers(&cache) == 0);
        // TODO: add VRAM/resource cache check here
    }
}

#endif
