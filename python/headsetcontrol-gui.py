#!/usr/bin/python

# graphical user interface for headsetcontrol

import argparse
import subprocess
import sys

try :
    import gi
    gi.require_version('Gtk', '3.0')
    from gi.repository import Gtk
    from gi.repository import GLib
except :
    sys.exit("import failed: please ensure that you have the gi (Python API for Introspection) library installed")

# GUI -- mainwindow
class MainWindow(Gtk.ApplicationWindow):
    def quit(self, button) :
        Gtk.main_quit()
        
    def __init__(self, title, capabilities, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.set_title(title)
        
        self.outerbox = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        self.outerbox.set_spacing(20)
        self.add(self.outerbox)

        self.innerframe = Gtk.Frame()
        self.innerframe.set_shadow_type(Gtk.ShadowType.NONE)
        self.outerbox.add(self.innerframe)
        
        self.grid = Gtk.Grid()
        self.innerframe.add(self.grid)

        # we'll sort the capabilities so user-modifiable parameters come first
        row = 0
        for cap in capabilities :
            if cap.editable :
                self.grid.attach(cap.label, 0, row, 1, 1)
                self.grid.attach(cap.actuator, 1, row, 1, 1)
                cap.row = row
                row = row + 1
                
        for cap in capabilities :
            if not cap.editable :
                self.grid.attach(cap.label, 0, row, 1, 1)
                self.grid.attach(cap.actuator, 1, row, 1, 1)
                cap.row = row
                row = row + 1
        
        self.innerbox = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        self.innerbox.set_halign(Gtk.Align.END)
        self.outerbox.pack_start(self.innerbox, False, False, 0)
        
        self.button = Gtk.Button(label="Quit")
        self.button.connect('clicked', self.quit)
        self.innerbox.pack_start(self.button, False, False, 0)

# Interaction with eadset capabilities
class Capability :
    def __init__(self, short_option, long_option, argument, description, max=0, default=0, editable=True) :
        self.short_option = short_option
        self.long_option = long_option
        self.argument = argument
        self.description = description
        self.default = default
        self.max = max
        self.editable = editable
        self.label = Gtk.Label(label=long_option)
        self.label.set_halign(Gtk.Align.START)

        if editable :
            if type(self.default) == str:
                self.actuator = Gtk.Entry()
                self.actuator.set_hexpand(True)
                self.actuator.connect("activate", self.onEntry)
            elif self.max == 1 :
                self.actuator = Gtk.Switch()
                self.actuator.set_active(default == 1)
                self.actuator.set_hexpand(False)
                self.actuator.connect("notify::active", self.onSwitch)
            elif self.max < 10 :
                self.adjustment = Gtk.Adjustment(value=0, lower=0, upper=max, step_increment=1)
                self.actuator = Gtk.SpinButton(adjustment=self.adjustment, climb_rate=1, digits=0)
                self.actuator.set_value(default)
                self.actuator.set_hexpand(True)
                self.actuator.connect("value-changed", self.onSpin)
            else :
                self.actuator = Gtk.Scale()
                self.actuator.set_range(0, self.max)
                self.actuator.set_digits(0)
                self.actuator.set_draw_value(True)
                self.actuator.set_value(default)
                self.actuator.connect("value-changed", self.onChangeValue)
                self.actuator.set_hexpand(True)

            self.label.set_tooltip_text(description)
            self.actuator.set_tooltip_text(description)

        else:
            # If the level is between 0 and 100 we'll display it;
            # if it's less than 0 we'll display a label saying it's charging
            # hopefully nothing but the battery will ever return a value less than 0
            self.displayLevel = Gtk.ProgressBar()
            self.displayLevel.set_show_text = True
            self.displayLevel.text = ''
            self.displayLevel.set_fraction(default/100.0)
            self.displayLevel.set_hexpand(True)

            self.displayCharging = Gtk.Label()
            self.displayCharging.set_text("CHARGING")
            self.displayCharging.set_hexpand(True)

            self.actuator = self.displayLevel

    # Read-only capability
    def setPercent(self, value) :
        self.actuator.set_fraction(int(value)/100.0)

    # String capability (actuator is a Gtk.Entry)
    def onEntry(self, entry) :
        sendHeadset(self.short_option, entry.get_text())
    
    # Boolean capability (actuator is a Gtk.Switch)
    def onSwitch(self, switch, state) :
        print("switch")
        if switch.get_active() :
            sendHeadset(self.short_option, 1)
        else :
            sendHeadset(self.short_option, 0)        
    
    # Selector capability (actuator is a Gtk.SpinButton)
    def onSpin(self, spinButton) :
        sendHeadset(self.short_option, int(self.actuator.get_value()))
    
    # Fraction capability (actuator is a Gtk.Scale)
    def onChangeValue(self, scroll) :
        sendHeadset(self.short_option, str(int(self.actuator.get_value())))

class Capabilities :
    def __init__(self, capabilitiesList = []) :
        self.cap_dict = {}
        self.key_list = []
        for capability in capabilitiesList :
            self.insert(capability)
        
    def insert(self, capability) :
        self.key_list.append(capability.short_option)
        self.cap_dict[capability.short_option] = capability

    def lookup(self, key) :
        return self.cap_dict.get(key)

    def __len__(self) :
        return len(self.cap_dict)

    def __iter__(self) :
        return CapabilitiesIterator(self)
    
class CapabilitiesIterator :
    def __init__(self, capabilities) :
        self.keys = capabilities.key_list
        self.capabilities = capabilities
        self.current_index = 0

    def __iter__(self) :
        return self

    def __next__(self) :
        if self.current_index < len(self.capabilities) :
            self.cap = self.capabilities.lookup(self.keys[self.current_index])
            self.current_index = self.current_index + 1
            return self.cap
        raise StopIteration

class ErrorDialog(Gtk.MessageDialog) :
    def __init__(self, message) :
        super().__init__(title="Unable to Communicate With Headset",
                         text="Unable to Communicate With Headset",
                         parent = None,
                         buttons = Gtk.ButtonsType.OK)
        self.format_secondary_text(message)
        self.run()
        self.destroy()

def checkBattery() :
    cap = headset_capabilities.lookup('b')
    level = sendHeadset(cmd='b')
    if level is not None :
        if int(level) >= 0 :
            if cap.actuator != cap.displayLevel :
                mainWin.grid.remove(cap.actuator)
                cap.actuator = cap.displayLevel
                mainWin.grid.attach(cap.actuator, 1, cap.row, 1, 1)
                mainWin.show_all()
            cap.setPercent(level)
        else :
            if cap.actuator != cap.displayCharging :
                mainWin.grid.remove(cap.actuator)
                cap.actuator = cap.displayCharging
                mainWin.grid.attach(cap.actuator, 1, cap.row, 1, 1)
                mainWin.show_all()
                
    # set timer to check again
    GLib.timeout_add(args.batterypolltime * 1000, checkBattery)

def checkChatMix() :
    cap = headset_capabilities.lookup('m')
    level = sendHeadset(cmd='m')
    if level is not None :
        cap.setPercent(level)

    # set timer to check again
    GLib.timeout_add(args.chatmixpolltime * 1000, checkChatMix)
    
# Comm with headset
def sendHeadset(cmd=None, arg=None) :
        
    # make it easier to parse the result
    optionList = ['-c']
    
    # add command and arg if any
    optionList.append('-' + cmd)
    
    if arg is not None :
        optionList.append(arg)

    # and send it
    cmdline = ['headsetcontrol'] + optionList


    if args.debug :
        print("command line: " + str(cmdline))
        
    if (cmd == '?') or (headset_capabilities.lookup(cmd) is not None) :
        result = subprocess.run(cmdline, capture_output=True)

        if result.returncode == 0 :
            return result.stdout.decode()
        else :
            ErrorDialog(result.stderr.decode())
        return None
    elif args.debug :
        print("   unsupported")

    return None

# Main program

# parse command line
parser = argparse.ArgumentParser(prog="headsetcontrol_gui.py",
                                 description="GUI front end to headsetcontrol",
                                 epilog="requires gtk")
parser.add_argument("-b", "--batterypolltime", required=False, default=60, type=int, help="battery poll time in seconds (default 60; 0 for no poll)")
parser.add_argument("-c", "--chatmixpolltime", required=False, default=1, type=int, help="chat mix poll time in seconds (default 1; 0 for no poll)")
parser.add_argument("-d", "--debug", required=False, default=False, action="store_true", help="debug")

args = parser.parse_args()

if (args.debug) :
    print("debug enabled: " + str(args.debug))

# construct headset capabilities table
# headsetcontrol program options -t and -f are not supported.
all_capabilities = Capabilities([
    Capability('s', 'sidetone',         'level',          'Sets sidetone, level must be between 0 and 128', max=128),
    Capability('b', 'battery',          '',		   'Checks the battery level', max=100, editable=False),
    Capability('n', 'notificate',       'soundid', 	   'Makes the all play a notifiation', max=10),
    Capability('l', 'light',            '0|1', 	   'Switch lights (0 = off, 1 = on)', max=1),
    Capability('i', 'inactive-time',    'time', 	   'Sets inactive time in minutes, time must be between 0 and 90, 0 disables the feature', max=90),
    Capability('m', 'chatmix',          '',   	           'Retrieves the current chat-mix-dial level setting between 0 and 128. Below 64 is the game side and above is the chat side', editable=False, max=128),
    Capability('v', 'voice-prompt',     '0|1', 	   'Turn voice prompts on or off (0 = off, 1 = on)', max=1),
    Capability('r', 'rotate-to-mute ',  '0|1', 	   'Turn rotate to mute feature on or off (0 = off, 1 = on)', max=1),
    Capability('e', 'equalizer',        'string', 	   'Sets equalizer to specified curve, string must contain band values specific to the device (hex or decimal) delimited by spaces, or commas, or new-lines e.g "0x18, 0x18, 0x18, 0x18, 0x18"', default=""),
    Capability('p', 'equalizer-preset', 'number',         'Sets equalizer preset, number must be between 0 and 3, 0 sets the default', max=3),
#    Capability('f', 'follow',           '[secs timeout]', 'Re-run the commands after the specified seconds timeout or 2 by default', default=2, max=5),
#    Capability('t', 'timeout',          '(0-100000)',     'Specifies the timeout in ms for reading data from device (default 5000)', default=5000, max=100000)
])

# obtain headset name.  Needs verbose output to print name
result  = subprocess.run(['headsetcontrol', '-?'], stdout=subprocess.PIPE)
split = result.stdout.decode().split('Found ')
name = split[1].split("!")[0]

if (args.debug) :
    print("name " + name)

# ask headset capabilities
headset_capabilities = Capabilities()

result = sendHeadset(cmd='?')
split = result.split('\n')
for char in split[0] :
    headset_capabilities.insert(all_capabilities.lookup(char))
        
if args.debug :
    for cap in headset_capabilities :
        print(cap.long_option)

# set up GUI.
if not args.debug :
    mainWin = MainWindow(name, headset_capabilities)
else :
    mainWin = MainWindow(name, all_capabilities)
    
# start battery check and chat mix timer loops
if (args.batterypolltime > 0 ) and (headset_capabilities.lookup('b') or args.debug) :
    checkBattery()

if (args.chatmixpolltime > 0) and (headset_capabilities.lookup('m') or args.debug) :
    checkChatMix()

# and go!
mainWin.show_all()
Gtk.main()
