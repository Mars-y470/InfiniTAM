// Copyright Oxford University Innovation Limited and the authors of InfiniTAM

#pragma once

#include "ImageSourceEngine.h"

namespace rs2 { class pipeline; class context; class device; }

namespace InputSource {

    class RealSense2Engine : public BaseImageSourceEngine
    {
    private:
        bool dataAvailable;
        std::unique_ptr<rs2::context> ctx;
        std::unique_ptr<rs2::device> device;
        std::unique_ptr<rs2::pipeline> pipe;
        
        Vector2i imageSize_rgb, imageSize_d;
        
    public:
        RealSense2Engine(const char *calibFilename, bool alignColourWithDepth = true,
                         Vector2i imageSize_rgb = Vector2i(640, 480), Vector2i imageSize_d = Vector2i(640, 480));
        ~RealSense2Engine();
        
        bool hasMoreImages(void) const;
        void getImages(ITMUChar4Image *rgb, ITMShortImage *rawDepth);
        Vector2i getDepthImageSize(void) const;
        Vector2i getRGBImageSize(void) const;
    };

}
