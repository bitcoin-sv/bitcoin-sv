# Core dumps

This document describes options that allow us to enable generating core dumps on Linux operating system. They contain detailed information about the state of the application at a time when crash occurred and can can be useful for debugging purposes.

Since BitcoinSV Node software can run on different Linux distributions and also inside Docker container this document contains information about settings that we have to use in our systems to be able to produce a core dump.

The information about location where core dumps are saved is in file `/proc/sys/kernel/core_pattern`. It can be an absolute path, relative path or (if starting with `|`) location of the program that will receive core dump as a standard input.

### Change core_pattern permanently

Editing `/proc/sys/kernel/core_pattern` file directly will change this setting until next restart. \
To change it permanently you have to:

1.) Add `kernel.core_pattern = <core_dump_location>` to `/etc/sysctl.conf`.
- Recommended setting for `<core_dump_location>` is `/tmp/core.%e.%p.%t`.

2.) Run `sudo sysctl --system` for new setting to become active without restarting the machine.

> Note: Some Linux distributions delete files in `/tmp` folder after restart. If you want to keep your core dump make sure to copy it somewhere else.

### Ubuntu

Ubuntu's default core_pattern setting is: `|/usr/share/apport/apport %p %s %c %d %P %E`.

Changing core_pattern permanently requires additional step.

Change `enabled` setting in `/etc/default/apport` from `1` to `0`. By doing this we disable apport.

> Note: Using apport (even if enabled and with high ulimit) may result in not producing core dump due to its size.

## Running BSV node directly on host machine

The maximum size of core files is limited by `ulimit -c` setting. On many distributions, the default value is set to 0, and that disables the creation of core dump.

To change it temporarily (until user logs out):

```bash
ulimit -c unlimited
```

To make a permanent change you have to edit `/etc/security/limits.conf` file (Changes will affect all new sessions. You have to log out and login again).

Example of line that is added to the limit.conf file:

```bash
*       soft    core        unlimited
```

> Note: Setting limits with `*` does not affect `root` user. In order to set core limit for `root` user you have to add following line:
> ```bash
> root       soft    core        unlimited ```

It increases soft limit for core files to unlimited for all users.

> Note: Editing /etc/security/limits.conf and /proc/sys/kernel/core_pattern requires root permissions.

> Note: soft limit can be changed by the user, but cannot exceed the hard limit.

When running BitcoinSV Node directly on your machine you have to set your `ulimit -c` to high enough / unlimited value (see instructions above).

`core_pattern` (explanations above) tells you where core files will be generated (assuming you set ulimit -c to high enough value).

In case your core_pattern file contains an absolute path you can find a core dump there.

Some distributions have default `core_pattern` set to pipe core dump to some of their default core dump handling application.

## Running BSV node in Docker

When running BitcoinSV Node in Docker container, whether core dump will or won't be generated, depends on settings on the host machine.

`/proc/sys/kernel/core_pattern` is a global setting and does not support setting it separately per container.

If `kernel.core_pattern` value is an absolute path, core file is generated __inside__ container's file system.

You can copy it from the container with the following command:

```docker
docker cp <container_name>:<path_to_core_dump> <location_on_host_machine>
```

> Note: the path to core dump is defined by `/proc/sys/kernel/core_pattern` on the host machine.

> Note: The container should not be started with --rm flag (--rm flag causes deleting container's filesystem after container exits and this prevents us from copying core dump file).

> Note: docker cp command does not support wildcards. We recommend copying whole directory with core dumps.