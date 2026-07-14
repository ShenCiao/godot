# SCons build defaults shared by every Ciallo build (CI + local VSCode tasks).
#
# SConstruct auto-loads this file (see `customs = ["custom.py"]`), and command-line
# arguments still override anything set here. Keep ONLY options that are identical
# across all platforms and every target (editor / template_debug / template_release).
# Options that differ per target/platform stay on the command line, e.g.:
#   - production / debug_symbols          (editor omits production; templates use it)
#   - d3d12 (Windows) / vulkan (macOS)    (SDK-dependent)
#   - windows_subsystem, dev_build        (editor-only conveniences)
#   - disable_physics_3d / disable_navigation_3d  (rejected on editor builds,
#     only valid for export templates)

# Ciallo is a 2D-focused paint app: keep 3D rendering + Skeleton3D to render reference.
# Do not use `deprecated="no"` to avoid breaking mcp tool.

# Scripting runtime.
module_mono_enabled = "yes"  # Ciallo is a C#/.NET project, never disable this even for testing.

# Rendering and XR.
opengl3 = "no"  # Remove the OpenGL 3 / Compatibility renderer.
disable_xr = "yes"  # Remove XR nodes, servers, and XR-dependent modules.

# Input and accessibility.
sdl = "no"  # Remove SDL3 input, including SDL gamepad/controller support.
accesskit = "no"  # screen readers in a paint app for blind, really?

# 3D scene and import features not needed by the reference renderer.
module_csg_enabled = "no"  # Remove constructive solid geometry nodes.
module_gridmap_enabled = "no"  # Remove the 3D GridMap node and editor tools.
module_fbx_enabled = "no"  # Remove FBX/UFBX scene importing and FBX APIs.
module_meshoptimizer_enabled = "no"  # Remove 3D mesh simplification and cache optimization.

# 3D baking, occlusion, and collision-generation tools.
module_lightmapper_rd_enabled = "no"  # Remove GPU 3D lightmap baking.
module_xatlas_unwrap_enabled = "no"  # Remove automatic lightmap UV2 unwrapping.
module_raycast_enabled = "no"  # Remove Embree raycasting, occlusion, and lightmap support.
module_vhacd_enabled = "no"  # Remove 3D mesh convex decomposition.

# 3D simulation backends. Editor builds fall back to dummy servers.
module_godot_physics_3d_enabled = "no"  # Remove the GodotPhysics3D backend.
module_jolt_physics_enabled = "no"  # Remove the Jolt Physics backend.
module_navigation_3d_enabled = "no"  # Remove the GodotNavigation3D backend.

# Media capture and playback.
module_camera_enabled = "no"  # Remove CameraServer camera-feed support.
module_interactive_music_enabled = "no"  # Remove interactive music stream playback.

# Editor-only online services.
engine_update_check = "no"  # Remove automatic engine update checks in Project Manager.
