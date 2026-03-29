cmake_minimum_required(VERSION 3.21)

project(
	dice_roller
	LANGUAGES    C CXX ASM
	VERSION      1.0.0
	DESCRIPTION  "dice_roller"
	HOMEPAGE_URL "http://lameguy64.net/?page=psn00bsdk"
)

file(GLOB _sources *.c)
file(GLOB _lib_sources libs/*.c)

psn00bsdk_add_executable(dice_roller GPREL ${_sources} ${_lib_sources})

#region images
psn00bsdk_target_incbin(dice_roller PRIVATE tim_dice_atlas assets/dice_atlas.tim)
#endregion

psn00bsdk_add_cd_image(
	iso         # Target name
	dice_roller # Output file name (= dice_roller.bin + dice_roller.cue)
	iso.xml     # Path to config file
	DEPENDS dice_roller system.cnf
)
