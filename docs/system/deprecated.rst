Deprecated features
===================

In general features are intended to be supported indefinitely once
introduced into QEMU. In the event that a feature needs to be removed,
it will be listed in this section. The feature will remain functional for the
release in which it was deprecated and one further release. After these two
releases, the feature is liable to be removed. Deprecated features may also
generate warnings on the console when QEMU starts up, or if activated via a
monitor command, however, this is not a mandatory requirement.

Prior to the 2.10.0 release there was no official policy on how
long features would be deprecated prior to their removal, nor
any documented list of which features were deprecated. Thus
any features deprecated prior to 2.10.0 will be treated as if
they were first deprecated in the 2.10.0 release.

What follows is a list of all features currently marked as
deprecated.

System emulator command line arguments
--------------------------------------

``QEMU_AUDIO_`` environment variables and ``-audio-help`` (since 4.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The ``-audiodev`` argument is now the preferred way to specify audio
backend settings instead of environment variables.  To ease migration to
the new format, the ``-audiodev-help`` option can be used to convert
the current values of the environment variables to ``-audiodev`` options.

Creating sound card devices and vnc without ``audiodev=`` property (since 4.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

When not using the deprecated legacy audio config, each sound card
should specify an ``audiodev=`` property.  Additionally, when using
vnc, you should specify an ``audiodev=`` property if you plan to
transmit audio through the VNC protocol.

Creating sound card devices using ``-soundhw`` (since 5.1)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Sound card devices should be created using ``-device`` instead.  The
names are the same for most devices.  The exceptions are ``hda`` which
needs two devices (``-device intel-hda -device hda-duplex``) and
``pcspk`` which can be activated using ``-machine
pcspk-audiodev=<name>``.

``-chardev`` backend aliases ``tty`` and ``parport`` (since 6.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

``tty`` and ``parport`` are aliases that will be removed. Instead, the
actual backend names ``serial`` and ``parallel`` should be used.

Short-form boolean options (since 6.0)
''''''''''''''''''''''''''''''''''''''

Boolean options such as ``share=on``/``share=off`` could be written
in short form as ``share`` and ``noshare``.  This is now deprecated
and will cause a warning.

``delay`` option for socket character devices (since 6.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The replacement for the ``nodelay`` short-form boolean option is ``nodelay=on``
rather than ``delay=off``.

``--enable-fips`` (since 6.0)
'''''''''''''''''''''''''''''

This option restricts usage of certain cryptographic algorithms when
the host is operating in FIPS mode.

If FIPS compliance is required, QEMU should be built with the ``libgcrypt``
library enabled as a cryptography provider.

Neither the ``nettle`` library, or the built-in cryptography provider are
supported on FIPS enabled hosts.

``-writeconfig`` (since 6.0)
'''''''''''''''''''''''''''''

The ``-writeconfig`` option is not able to serialize the entire contents
of the QEMU command line.  It is thus considered a failed experiment
and deprecated, with no current replacement.

Userspace local APIC with KVM (x86, since 6.0)
''''''''''''''''''''''''''''''''''''''''''''''

Using ``-M kernel-irqchip=off`` with x86 machine types that include a local
APIC is deprecated.  The ``split`` setting is supported, as is using
``-M kernel-irqchip=off`` with the ISA PC machine type.

hexadecimal sizes with scaling multipliers (since 6.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''

Input parameters that take a size value should only use a size suffix
(such as 'k' or 'M') when the base is written in decimal, and not when
the value is hexadecimal.  That is, '0x20M' is deprecated, and should
be written either as '32M' or as '0x2000000'.

``-spice password=string`` (since 6.0)
''''''''''''''''''''''''''''''''''''''

This option is insecure because the SPICE password remains visible in
the process listing. This is replaced by the new ``password-secret``
option which lets the password be securely provided on the command
line using a ``secret`` object instance.

``opened`` property of ``rng-*`` objects (since 6.0.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''

The only effect of specifying ``opened=on`` in the command line or QMP
``object-add`` is that the device is opened immediately, possibly before all
other options have been processed.  This will either have no effect (if
``opened`` was the last option) or cause errors.  The property is therefore
useless and should not be specified.

``loaded`` property of ``secret`` and ``secret_keyring`` objects (since 6.0.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The only effect of specifying ``loaded=on`` in the command line or QMP
``object-add`` is that the secret is loaded immediately, possibly before all
other options have been processed.  This will either have no effect (if
``loaded`` was the last option) or cause options to be effectively ignored as
if they were not given.  The property is therefore useless and should not be
specified.


QEMU Machine Protocol (QMP) commands
------------------------------------

``blockdev-open-tray``, ``blockdev-close-tray`` argument ``device`` (since 2.8.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``eject`` argument ``device`` (since 2.8.0)
'''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``blockdev-change-medium`` argument ``device`` (since 2.8.0)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``block_set_io_throttle`` argument ``device`` (since 2.8.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument ``id`` instead.

``blockdev-add`` empty string argument ``backing`` (since 2.10.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use argument value ``null`` instead.

``block-commit`` arguments ``base`` and ``top`` (since 3.1.0)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use arguments ``base-node`` and ``top-node`` instead.

``nbd-server-add`` and ``nbd-server-remove`` (since 5.2)
''''''''''''''''''''''''''''''''''''''''''''''''''''''''

Use the more generic commands ``block-export-add`` and ``block-export-del``
instead.  As part of this deprecation, where ``nbd-server-add`` used a
single ``bitmap``, the new ``block-export-add`` uses a list of ``bitmaps``.

System accelerators
-------------------

MIPS ``Trap-and-Emul`` KVM support (since 6.0)
''''''''''''''''''''''''''''''''''''''''''''''

The MIPS ``Trap-and-Emul`` KVM host and guest support has been removed
from Linux upstream kernel, declare it deprecated.

System emulator CPUS
--------------------

``Icelake-Client`` CPU Model (since 5.2.0)
''''''''''''''''''''''''''''''''''''''''''

``Icelake-Client`` CPU Models are deprecated. Use ``Icelake-Server`` CPU
Models instead.

MIPS ``I7200`` CPU Model (since 5.2)
''''''''''''''''''''''''''''''''''''

The ``I7200`` guest CPU relies on the nanoMIPS ISA, which is deprecated
(the ISA has never been upstreamed to a compiler toolchain). Therefore
this CPU is also deprecated.

System emulator machines
------------------------

Raspberry Pi ``raspi2`` and ``raspi3`` machines (since 5.2)
'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''

The Raspberry Pi machines come in various models (A, A+, B, B+). To be able
to distinguish which model QEMU is implementing, the ``raspi2`` and ``raspi3``
machines have been renamed ``raspi2b`` and ``raspi3b``.

Aspeed ``swift-bmc`` machine (since 6.1)
''''''''''''''''''''''''''''''''''''''''

This machine is deprecated because we have enough AST2500 based OpenPOWER
machines. It can be easily replaced by the ``witherspoon-bmc`` or the
``romulus-bmc`` machines.

Device options
--------------

Emulated device options
'''''''''''''''''''''''

``-device virtio-blk,scsi=on|off`` (since 5.0.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The virtio-blk SCSI passthrough feature is a legacy VIRTIO feature.  VIRTIO 1.0
and later do not support it because the virtio-scsi device was introduced for
full SCSI support.  Use virtio-scsi instead when SCSI passthrough is required.

Note this also applies to ``-device virtio-blk-pci,scsi=on|off``, which is an
alias.

Block device options
''''''''''''''''''''

``"backing": ""`` (since 2.12.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

In order to prevent QEMU from automatically opening an image's backing
chain, use ``"backing": null`` instead.

``rbd`` keyvalue pair encoded filenames: ``""`` (since 3.1.0)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Options for ``rbd`` should be specified according to its runtime options,
like other block drivers.  Legacy parsing of keyvalue pair encoded
filenames is useful to open images with the old format for backing files;
These image files should be updated to use the current format.

Example of legacy encoding::

  json:{"file.driver":"rbd", "file.filename":"rbd:rbd/name"}

The above, converted to the current supported format::

  json:{"file.driver":"rbd", "file.pool":"rbd", "file.image":"name"}

linux-user mode CPUs
--------------------

``ppc64abi32`` CPUs (since 5.2.0)
'''''''''''''''''''''''''''''''''

The ``ppc64abi32`` architecture has a number of issues which regularly
trip up our CI testing and is suspected to be quite broken. For that
reason the maintainers strongly suspect no one actually uses it.

MIPS ``I7200`` CPU (since 5.2)
''''''''''''''''''''''''''''''

The ``I7200`` guest CPU relies on the nanoMIPS ISA, which is deprecated
(the ISA has never been upstreamed to a compiler toolchain). Therefore
this CPU is also deprecated.

Related binaries
----------------

qemu-img amend to adjust backing file (since 5.1)
'''''''''''''''''''''''''''''''''''''''''''''''''

The use of ``qemu-img amend`` to modify the name or format of a qcow2
backing image is deprecated; this functionality was never fully
documented or tested, and interferes with other amend operations that
need access to the original backing image (such as deciding whether a
v3 zero cluster may be left unallocated when converting to a v2
image).  Rather, any changes to the backing chain should be performed
with ``qemu-img rebase -u`` either before or after the remaining
changes being performed by amend, as appropriate.

qemu-img backing file without format (since 5.1)
''''''''''''''''''''''''''''''''''''''''''''''''

The use of ``qemu-img create``, ``qemu-img rebase``, or ``qemu-img
convert`` to create or modify an image that depends on a backing file
now recommends that an explicit backing format be provided.  This is
for safety: if QEMU probes a different format than what you thought,
the data presented to the guest will be corrupt; similarly, presenting
a raw image to a guest allows a potential security exploit if a future
probe sees a non-raw image based on guest writes.

To avoid the warning message, or even future refusal to create an
unsafe image, you must pass ``-o backing_fmt=`` (or the shorthand
``-F`` during create) to specify the intended backing format.  You may
use ``qemu-img rebase -u`` to retroactively add a backing format to an
existing image.  However, be aware that there are already potential
security risks to blindly using ``qemu-img info`` to probe the format
of an untrusted backing image, when deciding what format to add into
an existing image.

Backwards compatibility
-----------------------

Runnability guarantee of CPU models (since 4.1.0)
'''''''''''''''''''''''''''''''''''''''''''''''''

Previous versions of QEMU never changed existing CPU models in
ways that introduced additional host software or hardware
requirements to the VM.  This allowed management software to
safely change the machine type of an existing VM without
introducing new requirements ("runnability guarantee").  This
prevented CPU models from being updated to include CPU
vulnerability mitigations, leaving guests vulnerable in the
default configuration.

The CPU model runnability guarantee won't apply anymore to
existing CPU models.  Management software that needs runnability
guarantees must resolve the CPU model aliases using the
``alias-of`` field returned by the ``query-cpu-definitions`` QMP
command.

While those guarantees are kept, the return value of
``query-cpu-definitions`` will have existing CPU model aliases
point to a version that doesn't break runnability guarantees
(specifically, version 1 of those CPU models).  In future QEMU
versions, aliases will point to newer CPU model versions
depending on the machine type, so management software must
resolve CPU model aliases before starting a virtual machine.

Guest Emulator ISAs
-------------------

nanoMIPS ISA
''''''''''''

The ``nanoMIPS`` ISA has never been upstreamed to any compiler toolchain.
As it is hard to generate binaries for it, declare it deprecated.
