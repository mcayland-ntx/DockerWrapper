# DockerWrapper: a wrapper for using nerdctl with the Jenkins docker-workflow-plugin on Windows

It creates a docker executable that acts as a passthrough for nerdctl except for when invoked as
`docker --version`, at which point it injects a higher version number into the output so that the
docker-workflow-plugin will use the working codepath for Windows builds.

See https://github.com/jenkinsci/docker-workflow-plugin/pull/370 for more background and an upstream patch that should also work around it.
