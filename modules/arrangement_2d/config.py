def can_build(env, platform):
    if env["target"] != "editor":
        return False

    if platform == "macos":
        return env["arch"] == "arm64"

    return platform in ["windows", "linuxbsd"] and env["arch"] == "x86_64"


def configure(env):
    pass
