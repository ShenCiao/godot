# SCons build defaults shared by every Ciallo build (CI + local VSCode tasks).
#
# SConstruct auto-loads this file (see `customs = ["custom.py"]`), and command-line
# arguments still override anything set here. Keep ONLY options that are identical
# across all platforms and every target (editor / template_debug / template_release).
#
# NOTE: the filename must stay lowercase. Linux/macOS CI is case-sensitive and
# SConstruct looks for "custom.py"; a capitalized "Custom.py" is silently ignored
# there (it only worked locally because Windows is case-insensitive).
#
# Options that differ per target/platform stay on the command line, e.g.:
#   - production / debug_symbols          (editor omits production; templates use it)
#   - d3d12 (Windows) / vulkan (macOS)    (SDK-dependent)
#   - windows_subsystem, dev_build        (editor-only conveniences)
#   - disable_physics_3d / disable_navigation_3d  (rejected on editor builds,
#     only valid for export templates)

module_mono_enabled = "yes"

# Ciallo is a 2D-focused engine: keep 3D rendering + Skeleton3D, drop the rest we
# don't ship. See also the "editor vs template 3D divergence" project memory.
deprecated = "no"
accesskit = "no"
opengl3 = "no"
disable_xr = "yes"
module_csg_enabled = "no"
module_gridmap_enabled = "no"
