#pragma once
#include "window_control.hpp"
#include <openxr/openxr.h>

// One compositor layer for a vertical slice of the screen (or of something laid over it, like the ambient glow): a flat
// quad, or a section of a cylinder when the screen is curved. Only one of the two is used for a given slot.
struct ScreenLayer {
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    XrCompositionLayerCylinderKHR cylinder{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
    bool curved=false;
    const XrCompositionLayerBaseHeader* header() const {
        return curved ? reinterpret_cast<const XrCompositionLayerBaseHeader*>(&cylinder) : reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
    }
};

// `centre` is where the slice's middle lies along the screen, in unrolled metres from the window's centre line;
// `sliceWidth` and `height` are its size measured along the surface. radius<=0 gives a flat quad in the window plane.
//
// For a curved slice, the OpenXR cylinder layer's pose is taken to be on the visible surface, facing the viewer, like a
// quad's. The specification's wording ("the center point of the view of the cylinder") can also be read as the
// cylinder's axis, so poseAtAxis=true places the pose there instead: the same screen, one switch away, in case a runtime
// reads it that way.
inline void fillScreenLayer(ScreenLayer& layer,XrSpace space,const XrPosef& window,float radius,bool poseAtAxis,float centre,
    float sliceWidth,float height,XrSwapchain swapchain,const XrRect2Di& rect,XrEyeVisibility eye,XrCompositionLayerFlags flags) {
    layer.curved=radius>0;
    XrPosef pose=onScreen(window,radius,centre,0);
    if (!layer.curved) {
        auto& q=layer.quad;
        q=XrCompositionLayerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
        q.layerFlags=flags; q.space=space; q.eyeVisibility=eye; q.subImage.swapchain=swapchain; q.subImage.imageRect=rect;
        q.pose=pose; q.size={sliceWidth,height};
        return;
    }
    if (poseAtAxis) pose.position=pose::add(pose.position,pose::rotate(pose.orientation,{0,0,radius}));
    auto& c=layer.cylinder;
    c=XrCompositionLayerCylinderKHR{XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR};
    c.layerFlags=flags; c.space=space; c.eyeVisibility=eye; c.subImage.swapchain=swapchain; c.subImage.imageRect=rect;
    c.pose=pose; c.radius=radius; c.centralAngle=sliceWidth/radius; c.aspectRatio=sliceWidth/height;
}
