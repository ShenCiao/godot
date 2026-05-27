def can_build(env, platform):
    return platform == "windows" and env["arch"] == "x86_64" and env["target"] == "editor"


def configure(env):
    pass
