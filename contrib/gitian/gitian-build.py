#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
from packaging import version
import distro
import shutil

### Building of local code
# This script is able to build local code which is not supported by gitian-builder.
# To overcome this, we use a modified gbuild script (gbuild_local)

### Known issues
# - When doing a local build, files created by gitian-builder will be copied to bitcoin-binaries/<version> folder.
#   Folder <version> will be called what 'git describe' returns, but the content of this folder may contain files
#   named with a different version, for example folder 'v1.0.6.beta-1115-gb1798385e' will contain 'bitcoin-sv-1.0.8.tar.gz'.
#   This is because version number is stored in multiple places and is edited manually before each release and could be
#   different of the recent tag.

### Troubleshooting notes:
# - If local build fails, make sure to have a clean repository from previous builds, especially from cmake.
# - Autotools have issues if file paths are longer then 99 chars. Gitian won't exit at this point,
#   but will fail somewhere else, making it very hard to find the root cause
#   This is fixed by adding 'tar-pax' to AM_INIT_AUTOMAKE[] in configure.ac

def setup():
    print("Setup start")
    global args, workdir, commit_hash
    programs = ['ruby', 'git', 'apt-cacher-ng', 'make', 'wget', 'lxc', 'debootstrap']
    subprocess.check_call(['sudo', 'apt-get', 'install', '-qq'] + programs)

    print("Cloning repos")
    # TODO: gitian.sigs repository is empty and we don't use it. Signing is done manually.
    if not os.path.isdir('gitian.sigs'):
        subprocess.check_call(['git', 'clone', 'https://github.com/bitcoin-sv/gitian.sigs.git'])
    if not os.path.isdir('gitian-builder'):
        subprocess.check_call(['git', 'clone', 'https://github.com/devrandom/gitian-builder.git'])
        # Checkout a specific commit. Since the last tag is 0.2 from year 2015 we use a later commit from master
        os.chdir('gitian-builder')
        subprocess.check_call(['git', 'fetch'])
        subprocess.check_call(['git', 'checkout', '32b1145'])
        os.chdir(workdir)

    os.chdir('gitian-builder')

    # Remove previous base VM. This prevents make-base-vm from failing in case old file exists
    ubuntu_version = args.ubuntu_version
    ubuntu_arch = 'amd64'
    vm_base_path = "base-"+ubuntu_version+"-"+ubuntu_arch
    if os.path.exists(vm_base_path):
        os.remove(vm_base_path)

    # Create base VM
    make_image_prog = ['bin/make-base-vm', '--suite', ubuntu_version, '--arch', ubuntu_arch]
    make_image_prog += ['--lxc']
    print("Running VM: %s" % make_image_prog)
    subprocess.check_call(make_image_prog)

    os.chdir(workdir)

    # Restart LXC network
    if version.parse(distro.version()) >= version.parse("18.04"):
        # Replaces a string in lxc-net configuration file from 'lxcbr0' to 'br0'
        # A better approach would be to fix this in provisioner.sh where this configuration is created.
        subprocess.check_call(['sudo', 'sed', '-i', 's/lxcbr0/br0/', '/etc/default/lxc-net'])
        # Restart lxc-net
        subprocess.check_call(['ip', 'a'])
        subprocess.run(['sudo', 'ip', 'link', 'set', 'lxcbr0', 'down'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run(['sudo', 'brctl', 'delbr', 'lxcbr0'], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.check_call(['sudo', 'systemctl', 'restart', 'lxc-net'])

def build():
    global args, workdir, srcdir, commit_hash

    print('Downloading dependencies')
    os.chdir('gitian-builder')
    os.makedirs('inputs', exist_ok=True)

    # Download dependencies
    # This is done by running 'make download' in the depends folder and saving it to 'bitcoin-builder/cache/common'
    # This step is also done during the build process in the VM but it fails to download the dependencies, because it has no internet access.
    # This is a workaround and it requires to clone the whole repository (unless doing a local build) and run 'make download' from the host machine.
    # TODO: We could avoid cloning the repo and downloading dependencies from the host, but would need to fix lxc network to have internet access.
    # To achieve this, changes to provisioner.sh would have to be made, but that could interfere with the legacy gitian-build.py script.

    # Set 'depends' dir for a local build
    depends_dir = srcdir + '/depends'
    # Set 'depends' dir for a remote build.
    if args.url:
        os.chdir(workdir)
        if os.path.isdir('bitcoin-sv'):
            shutil.rmtree('bitcoin-sv')
        subprocess.check_call(['git', 'clone', args.url, 'bitcoin-sv'])
        os.chdir('bitcoin-sv')
        subprocess.check_call(['git', 'checkout', args.version])
        depends_dir = workdir+'/bitcoin-sv/depends'
        srcdir = os.path.dirname(workdir+'/bitcoin-sv/')
        if os.path.isdir(depends_dir) is False:
            raise Exception("Depends folder %s doesn't exist. Did git checkout the code successfully?" % depends_dir)

        # Convert provided version to long hash. This is necessary because gitian is picky about how version are passed to it.
        # For example tags are fine but branches not if they have certain chars in the name. Long hash is OK but short is not.
        commit_hash = subprocess.check_output(['git', 'rev-parse', args.version]).decode("utf-8").strip()
        print("Version hash: %s" % commit_hash)

        os.chdir(workdir+'/gitian-builder')

    # Run make download
    subprocess.check_call(['make', '-C', depends_dir, 'download', 'SOURCES_PATH=' + os.getcwd() + '/cache/common'])

    # Build
    if args.linux:
        print('\nCompiling ' + args.version + ' Linux')
        if args.url is not None:
            # Remote build from --url
            subprocess.check_call(['bin/gbuild', '-j', args.jobs, '-m', args.memory, '--commit', 'bitcoin='+commit_hash, '--url', 'bitcoin='+args.url, srcdir+'/contrib/gitian-descriptors/' + args.linux_yml])
        else:
            # Local build
            subprocess.check_call(['bin/gbuild_local', '-j', args.jobs, '-m', args.memory, '--commit', 'bitcoin='+commit_hash, '--skip-fetch', srcdir+'/contrib/gitian-descriptors/' + args.linux_yml])

    if args.windows:
        print('\nCompiling ' + commit_hash + ' Windows')
        if args.url is not None:
            # Remote build from --url
            subprocess.check_call(['bin/gbuild', '-j', args.jobs, '-m', args.memory, '--commit', 'bitcoin='+commit_hash, '--url', 'bitcoin='+args.url, srcdir+'/contrib/gitian-descriptors/' + args.win_yml])
        else:
            # Local build
            subprocess.check_call(['bin/gbuild_local', '-j', args.jobs, '-m', args.memory, '--commit', 'bitcoin='+commit_hash, '--skip-fetch', srcdir+'/contrib/gitian-descriptors/' + args.win_yml])

    os.chdir(workdir)

def sign():
    print("Signing...")
    global args, workdir, commit_hash
    os.chdir('gitian-builder')
    
    if args.linux:
        subprocess.check_call(['bin/gsign', '-p', args.sign_prog, '--signer', args.sign, '--release', commit_hash + '-linux', '--destination', workdir+'/gitian.sigs/', srcdir+'/contrib/gitian-descriptors/' + args.linux_yml])

    if args.windows:
        print('\nSigning ' + commit_hash + ' Windows')
        subprocess.check_call('cp inputs/bitcoin-' + commit_hash + '-win-unsigned.tar.gz inputs/bitcoin-win-unsigned.tar.gz', shell=True)
        subprocess.check_call(['bin/gbuild', '--skip-image', '--commit', 'signature='+args.commit, '../bitcoin-sv/contrib/gitian-descriptors/gitian-win-signer.yml'])
        subprocess.check_call(['bin/gsign', '-p', args.sign_prog, '--signer', args.sign, '--release', commit_hash+'-win-signed', '--destination', '../gitian.sigs/', srcdir+'/gitian-descriptors/gitian-win-signer.yml'])

    os.chdir(workdir)

def verify():
    global args, workdir
    os.chdir('gitian-builder')
    if args.linux:
        print('\nVerifying v'+commit_hash+' Linux\n')
        subprocess.check_call(['bin/gverify', '-v', '-d', workdir+'/gitian.sigs/', '-r', commit_hash+'-linux', srcdir+'/contrib/gitian-descriptors/' + args.linux_yml])

    if args.windows:
        print('\nVerifying v'+commit_hash+' Windows\n')
        subprocess.check_call(['bin/gverify', '-v', '-d', workdir+'/gitian.sigs/', '-r', commit_hash+'-win-unsigned', srcdir+'/contrib/gitian-descriptors/' + args.win_yml])
        print('\nVerifying v'+commit_hash+' Signed Windows\n')
        subprocess.check_call(['bin/gverify', '-v', '-d', workdir+'/gitian.sigs/', '-r', commit_hash+'-win-signed', srcdir+'/contrib/gitian-descriptors/gitian-win-signer.yml'])

    os.chdir(workdir)

def copy_files():
    print("Copying files...")
    version_compatible = args.version.replace('/', '-')
    os.makedirs('bitcoin-binaries/' + version_compatible, exist_ok=True)
    os.chdir('gitian-builder')

    if args.linux:
        subprocess.check_call('cp build/out/bitcoin-*.tar.gz build/out/src/bitcoin-*.tar.gz result/bitcoin*.yml ../bitcoin-binaries/' + version_compatible, shell=True)

    if args.windows:
        subprocess.check_call('mv build/out/bitcoin-*-win-unsigned.tar.gz inputs/', shell=True)
        subprocess.check_call('mv build/out/bitcoin-*.zip build/out/bitcoin-*.exe result/bitcoin*.yml ../bitcoin-binaries/' + version_compatible, shell=True)

        if args.sign():
            subprocess.check_call('mv build/out/bitcoin-*win64-setup.exe ../bitcoin-binaries/' + version_compatible, shell=True)
            subprocess.check_call('mv build/out/bitcoin-*win32-setup.exe ../bitcoin-binaries/' + version_compatible, shell=True)

    os.chdir(workdir)

def commit_signitures():
    print('\nCommitting ' + commit_hash + ' Signed Sigs\n')
    os.chdir('gitian.sigs')
    subprocess.check_call(['git', 'add', commit_hash + '-linux/' + args.signer])
    subprocess.check_call(['git', 'add', commit_hash + '-win-unsigned/' + args.signer])
    subprocess.check_call(['git', 'commit', '-m', 'Add ' + commit_hash + ' unsigned sigs for ' + args.signer])
    subprocess.check_call(['git', 'add', commit_hash + '-win-signed/' + args.sign])
    subprocess.check_call(['git', 'commit', '-a', '-m', 'Add ' + commit_hash + ' signed binary sigs for ' + args.sign])

    os.chdir(workdir)

def main():
    global args, workdir, srcdir, commit_hash

    commit_hash = ''

    script_name = os.path.basename(sys.argv[0])

    # Check OS and version
    print("OS: %s %s" % (distro.version(), distro.id()))
    if distro.id() != 'ubuntu' or version.parse(distro.version()) < version.parse("18.04"):
        print("Warning, this script is tested on Ubuntu 18.04 and 20.04 and is not guaranteed to to work on %s %s" % (distro.id(), distro.version()))
        input("Press any key to continue or Ctrl+c to exit")

    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter,
        usage='%(prog)s [options]',
        epilog='Examples of use\n'
               'Setup:\n   ./' + script_name+' --setup\n'
               'Build remote (build only):\n  ./' + script_name+' --os l -j4 --url=https://github.com/bitcoin-sv/bitcoin-sv --version=v1.0.8 --build\n'
               'Build remote (complete):\n  ./' + script_name+' --os l -j4 --url=https://github.com/bitcoin-sv/bitcoin-sv --version=v1.0.8 --detach-sign --build --sign=support@bitcoinsv.io --verify\n'
               'Build local:\n  ./' + script_name + ' --os l -j4 --build\n'
        )
    parser.add_argument('-c', '--commit', action='store_true', dest='commit', help='Indicate that the version argument is for a commit or branch')
    parser.add_argument('-u', '--url', dest='url', default=None, help='Specify the URL of the repository. If unspecified, will build local code')
    parser.add_argument('-v', '--verify', action='store_true', dest='verify', help='Verify the Gitian build')
    parser.add_argument('-b', '--build', action='store_true', dest='build', help='Do a Gitian build')
    parser.add_argument('-o', '--os', dest='os', default='l', help='Specify which Operating Systems the build is for. Default is %(default)s. l for Linux, w for Windows')
    parser.add_argument('-j', '--jobs', dest='jobs', default='2', help='Number of processes to use. Default %(default)s')
    parser.add_argument('-m', '--memory', dest='memory', default='2000', help='Memory to allocate in MiB. Default %(default)s')
    parser.add_argument('-S', '--setup', action='store_true', dest='setup', help='Set up the Gitian building environment. Uses LXC.')
    parser.add_argument('-D', '--detach-sign', action='store_true', dest='detach_sign', help='Create the assert file for detached signing. Will not commit anything.')
    parser.add_argument('-n', '--commit-sigs', action='store_true', dest='commit_sigs', help='Commit signatures to git')
    parser.add_argument('-a', '--all-arch', action='store_true', dest='all_arch', help='Build for all supported architiectures')
    parser.add_argument('-s', '--sign', help='GPG signer to sign each build assert file')
    parser.add_argument('-e', '--version', help='Version number, commit, or branch to build. If building a commit or branch, the -c option must be specified')
    parser.add_argument('-r', '--release', dest='ubuntu_version', default='bionic', help='Ubuntu release to build under. Default is %(default)')
    args = parser.parse_args()

    # Fail if trying to commit signatures without signing
    if args.commit_sigs and args.sign is None and args.setup is False:
            raise Exception('Missing signer. Use --no-commit or --sign=<signer> options.')

    # Parse OS args
    args.linux = 'l' in args.os
    args.windows = 'w' in args.os

    # This is valid if this script is in contrib/gitian
    workdir = os.getcwd()
    gitian_builder_dir = os.path.dirname(workdir+"/gitian-builder/")
    if not os.path.isdir(gitian_builder_dir) and args.setup is False:
        raise Exception(("Error: path %s doesn't exist. Did you run ./%s --setup?" % (script_name, gitian_builder_dir)))
    srcdir = os.path.dirname(workdir+'/bitcoin-sv/')

    # TODO: all-arch fails. Are we using this at all?
    if args.all_arch:
        args.linux_yml = 'gitian-linux-all-arch.yml'
        args.win_yml = 'gitian-win-all-arch.yml'
    else:
        args.linux_yml = 'gitian-linux.yml'
        args.win_yml = 'gitian-win.yml'

    # Set environment variables USE_LXC to let gitian-builder know what VM to use
    os.environ['USE_LXC'] = '1'
    if not 'GITIAN_HOST_IP' in os.environ.keys():
        os.environ['GITIAN_HOST_IP'] = '10.0.3.1'
    if not 'LXC_GUEST_IP' in os.environ.keys():
        os.environ['LXC_GUEST_IP'] = '10.0.3.5'

    # This will pass the signing program to the 'gsign' script.
    # Passing program 'true' will make it do nothing
    args.sign_prog = 'true' if args.detach_sign else 'gpg --detach-sign'

    # Error if argument version is missing
    if args.url is not None and args.version is None:
        raise Exception('Missing argument --version')

    # Cleanup old build files
    if args.build:
        os.chdir(workdir)
        print("Removing old build files")
        if os.path.exists(gitian_builder_dir + '/inputs/bitcoin'):
            shutil.rmtree(gitian_builder_dir + '/inputs/bitcoin')

        # Setup for a local build. args.url==None means local
        if args.url is None and args.setup is False and args.version is None:
            srcdir = os.path.normpath(os.path.dirname(workdir+"/../../"))
            os.chdir(srcdir)
            
            # Get version using 'git describe'. Add '--abbrev=0' option to show recent tag only
            try:
                commit_hash = subprocess.check_output(['git', 'rev-parse', 'HEAD']).decode("utf-8").strip()
                args.version = subprocess.check_output(['git', 'describe', '--tags']).decode("utf-8").strip()
            except:
                commit_hash = "unknown"
                args.version = "unknown"

            print("Version: %s, hash: %s" % (args.version, commit_hash))

            # Make a copy of current code. Exclude current dir (gitian) to prevent copying within itself
            print("Making a copy of local code to: %s" % gitian_builder_dir + '/inputs/bitcoin')
            os.makedirs(gitian_builder_dir + '/inputs/bitcoin')
            exclude_dir = 'gitian'
            if workdir.endswith(exclude_dir) is False:
                raise Exception('Error: current working directory ' + workdir + ' is not as expected:' + exclude_dir)
            else:
                subprocess.check_call(['rsync', '-a', srcdir + '/', gitian_builder_dir + '/inputs/bitcoin', '--exclude', exclude_dir + '*'])

            # For a local build we use a modified gitian-builder/bin/gbuild script which allows to build local code. Copy it to gitian-builder/bin/gbuild_local
            shutil.copy(workdir+'/gbuild_local', gitian_builder_dir+'/bin/gbuild_local')

            os.chdir(workdir)

    os.chdir(workdir)

    if args.setup:
        setup()

    if args.build:
        build()

    if args.sign:
        sign()

    if args.verify:
        verify()

    if args.build and not args.setup:
        copy_files()

    if args.commit_sigs and not args.setup:
        commit_signitures()

if __name__ == '__main__':
    main()
