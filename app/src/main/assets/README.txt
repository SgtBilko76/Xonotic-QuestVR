XonoticQuest - Xonotic for Meta Quest (OpenXR)

Game data is NOT included. Copy the pk3 files from the official Xonotic 0.8.6
release (https://dl.xonotic.org/xonotic-0.8.6.zip) into:

    /sdcard/XonoticVR/data/      <- Xonotic/data/*.pk3 from the zip
    /sdcard/XonoticVR/key_0.d0pk <- Xonotic/key_0.d0pk

e.g. with adb:
    adb shell mkdir -p /sdcard/XonoticVR/data
    adb push Xonotic/data/*.pk3 /sdcard/XonoticVR/data/
    adb push Xonotic/key_0.d0pk /sdcard/XonoticVR/

commandline.txt holds the engine command line; data/vr.cfg holds the VR defaults
(both are only written once and are yours to edit).
