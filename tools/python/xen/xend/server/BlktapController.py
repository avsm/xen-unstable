# Copyright (c) 2005, XenSource Ltd.
import string, re
import popen2

from xen.xend.server.blkif import BlkifController
from xen.xend.XendLogging import log

phantomDev = 0;
phantomId = 0;

TAPDISK_SYSFS   = '/sys/class/blktap2'
TAPDISK_BINARY  = '/usr/sbin/tapdisk2'
TAPDISK_DEVICE  = '/dev/xen/blktap-2/tapdev'
TAPDISK_CONTROL = TAPDISK_SYSFS + '/blktap'

blktap_disk_types = [
    'aio',
    'sync',
    'vmdk',
    'ram',
    'qcow',
    'qcow2',
    'vhd',
    'ioemu',
    'tapdisk',
    ]
 
def doexec(args, inputtext=None):
    """Execute a subprocess, then return its return code, stdout and stderr"""
    proc = popen2.Popen3(args, True)
    if inputtext != None:
        proc.tochild.write(inputtext)
    stdout = proc.fromchild
    stderr = proc.childerr
    rc = proc.wait()
    return (rc,stdout,stderr)

def parseDeviceString(device):
    if device.find('/dev') == -1:
        raise Exception, 'invalid tap device: ' + device

    pattern = re.compile(TAPDISK_DEVICE + '(\d+)$')
    groups  = pattern.search(device)
    if not groups:
        raise Exception, 'malformed tap device: ' + device

    minor   = groups.group(1)
    control = TAPDISK_CONTROL + minor

    return minor, device, control



class BlktapController(BlkifController):
    def __init__(self, vm):
        BlkifController.__init__(self, vm)
        
    def frontendRoot(self):
        """@see DevController#frontendRoot"""
        
        return "%s/device/vbd" % self.vm.getDomainPath()

    def getDeviceDetails(self, config):
        (devid, back, front) = BlkifController.getDeviceDetails(self, config)

        phantomDevid = 0
        wrapped = False

        try:
            imagetype = self.vm.info['image']['type']
        except:
            imagetype = ""

        if imagetype == 'hvm':
            tdevname = back['dev']
            index = ['c', 'd', 'e', 'f', 'g', 'h', 'i', \
                     'j', 'l', 'm', 'n', 'o', 'p']
            while True:
                global phantomDev
                global phantomId
                import os, stat

                phantomId = phantomId + 1
                if phantomId == 16:
                    if index[phantomDev] == index[-1]:
                        if wrapped:
                            raise VmError(" No loopback block \
                                       devices are available. ")
                        wrapped = True
                        phantomDev = 0
                    else:
                        phantomDev = phantomDev + 1
                    phantomId = 1
                devname = 'xvd%s%d' % (index[phantomDev], phantomId)
                try:
                    info = os.stat('/dev/%s' % devname)
                except:
                    break

            vbd = { 'mode': 'w', 'device': devname }
            fn = 'tap:%s' % back['params']

            # recurse ... by creating the vbd, then fallthrough
            # and finish creating the original device

            from xen.xend import XendDomain
            dom0 = XendDomain.instance().privilegedDomain()
            phantomDevid = dom0.create_phantom_vbd_with_vdi(vbd, fn)
            # we need to wait for this device at a higher level
            # the vbd that gets created will have a link to us
            # and will let them do it there

        # add a hook to point to the phantom device,
        # root path is always the same (dom0 tap)
        if phantomDevid != 0:
            front['phantom_vbd'] = '/local/domain/0/backend/tap/0/%s' \
                                   % str(phantomDevid)

        return (devid, back, front)

    def createDevice(self, config):

        uname = config.get('uname', '')
        try:
            (typ, subtyp, params, file) = string.split(uname, ':', 3)
        except:
            (typ, params, file) = string.split(uname, ':', 2)
            subtyp = 'tapdisk'

        #check for blktap2 installation.
        blktap2_installed=0;
        (rc,stdout, stderr) = doexec("cat /proc/devices");
        out = stdout.read();
        stdout.close();
        stderr.close();
        if( out.find("blktap2") >= 0 ):
            blktap2_installed=1;
           
        if typ in ('tap'):
            if subtyp in ('tapdisk'):                                          
                if params in ('ioemu', 'qcow2', 'vmdk', 'sync') or not blktap2_installed:
                    log.warn('WARNING: using deprecated blktap module');
                    return BlkifController.createDevice(self, config);

        cmd = [ TAPDISK_BINARY, '-n', '%s:%s' % (params, file) ]
        (rc,stdout,stderr) = doexec(cmd)

        if rc != 0:
            err = stderr.read();
            out = stdout.read();
            stdout.close();
            stderr.close();
            raise Exception, 'Failed to create device.\n    stdout: %s\n    stderr: %s\nCheck that target \"%s\" exists and that blktap2 driver installed in dom0.' % (out.rstrip(), err.rstrip(), file);

        minor, device, control = parseDeviceString(stdout.readline())
        stdout.close();
        stderr.close();

        #modify the configuration to attach as a vbd, now that the
        #device is configured.  Then continue to create the device
        config.update({'uname' : 'phy:' + device.rstrip()})

        self.deviceClass='vbd'
        devid = BlkifController.createDevice(self, config)
        self.deviceClass='tap'
        return devid
