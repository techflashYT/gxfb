# GXFB

Experiment trying to use GX for an accelerated RGB framebuffer on the Nintendo GameCube/Wii.
3 versions are available:
* `efb-direct/`: Directly drawing into the EFB, then using GX to convert it into the XFB (**fastest**)
* `efb/`: Using a shadow buffer, then copying into the EFB, then using GX to convert it into the XFB
* `texture/`: Using a shadow buffer, tiling into a texture, then rendering that texture into the EFB, then using GX to convert it into the XFB
