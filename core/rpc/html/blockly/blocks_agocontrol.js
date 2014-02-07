/**
 * @info Blocky for agocontrol
 * @info tanguy.bonneau@gmail.com
 * @see block builder: https://blockly-demo.appspot.com/static/apps/blockfactory/index.html
 */

'use strict';

//====================================
//AGOCONTROL OBJECT
//====================================

var Agocontrol = {};
Agocontrol.schema = undefined;
Agocontrol.devices = undefined;

//init
Agocontrol.init = function(schema, devices) {
    //console.log(devices);
    Agocontrol.schema = schema;
    Agocontrol.devices = devices;
}

//get devices
Agocontrol.getDevices = function(deviceType) {
    var devices = [];
    var device = undefined;
    for( var i=0; i<Agocontrol.devices.length; i++ )
    {
        device = Agocontrol.devices[i];
        if( device.name.length>0 && device.devicetype==deviceType )
        {
            devices.push([device.name, device.uuid]);
        }
    }
    //prevent from js crash
    if( devices.length==0 )
        devices.push(['', '']);
    return devices;
}

//get device types
Agocontrol.getDeviceTypes = function() {
    var types = [];
    var duplicates = [];
    var device = undefined;
    for( var i=0; i<Agocontrol.devices.length; i++ )
    {
        device = Agocontrol.devices[i];
        if( device.name.length>0 && duplicates.indexOf(device.devicetype)==-1 )
        {
            types.push([device.devicetype, device.devicetype]);
            duplicates.push(device.devicetype);
        }
    }
    //prevent from js crash
    if( types.length==0 )
        types.push(['', '']);
    return types;
}

//get all events
Agocontrol.getAllEvents = function() {
    var events = [];
    for( var event in Agocontrol.schema.events )
    {
        events.push([event, event]);
    }
    //prevent from js crash
    if( events.length==0 )
        events.push(['', '']);
    return events;
}

//get device events
Agocontrol.getDeviceEvents = function(deviceType) {
    var events = [];
    if( Agocontrol.schema.devicetypes[deviceType]!==undefined && Agocontrol.schema.devicetypes[deviceType].events!==undefined )
    {
        for( var i=0; i<Agocontrol.schema.devicetypes[deviceType].events.length; i++ )
        {
            events.push([Agocontrol.schema.devicetypes[deviceType].events[i], Agocontrol.schema.devicetypes[deviceType].events[i]]);
        }
    }
    //prevent from js crash
    if( events.length==0 )
        events.push(['', '']);
    return events;
}

//get device properties
/*Agocontrol.getDeviceProperties = function(deviceType) {
    var props = [];
    if( Agocontrol.schema.devicetypes[deviceType]!==undefined && Agocontrol.schema.devicetypes[deviceType].properties!==undefined )
    {
        for( var prop in Agocontrol.schema.devicetypes[deviceType].properties )
        {
            props.push(prop);
            events.push([Agocontrol.schema.devicetypes[deviceType].events[i], Agocontrol.schema.devicetypes[deviceType].events[i]]);
        }
    }
    //prevent from js crash
    if( events.length==0 )
        events.push(['no event', '']);
    return events;
}*/

//get device events
Agocontrol.getDeviceEvents = function(deviceType) {
    var events = [];
    if( Agocontrol.schema.devicetypes[deviceType]!==undefined && Agocontrol.schema.devicetypes[deviceType].events!==undefined )
    {
        for( var i=0; i<Agocontrol.schema.devicetypes[deviceType].events.length; i++ )
        {
            events.push([Agocontrol.schema.devicetypes[deviceType].events[i], Agocontrol.schema.devicetypes[deviceType].events[i]]);
        }
    }
    //prevent from js crash
    if( events.length==0 )
        events.push(['no event', '']);
    return events;
}

//get device properties
Agocontrol.getDeviceProperties = function(deviceType) {
    var props = {};
    if( Agocontrol.schema.devicetypes[deviceType]!==undefined && Agocontrol.schema.devicetypes[deviceType].properties!==undefined )
    {
        var prop = undefined;
        for( var i=0; i<Agocontrol.schema.devicetypes[deviceType].properties.length; i++ )
        {
            prop = Agocontrol.schema.devicetypes[deviceType].properties[i];
            if( Agocontrol.schema.values[prop]!==undefined )
            {
                if( Agocontrol.schema.values[prop].type!==undefined && Agocontrol.schema.values[prop].name!==undefined )
                {
                    var content = {};
                    content['id'] = prop;
                    for( var item in  Agocontrol.schema.values[prop] )
                    {
                        content[item] = Agocontrol.schema.values[prop][item];
                    }
                    props[Agocontrol.schema.values[prop].name] = content;
                }
            }
        }
    }
    return props;
}

//get device commands
Agocontrol.getDeviceCommands = function(deviceType) {
    var cmds = {};
    if( Agocontrol.schema.devicetypes[deviceType]!==undefined && Agocontrol.schema.devicetypes[deviceType].commands!==undefined )
    {
        var cmd = undefined;
        for( var i=0; i<Agocontrol.schema.devicetypes[deviceType].commands.length; i++ )
        {
            cmd = Agocontrol.schema.devicetypes[deviceType].commands[i];
            if( Agocontrol.schema.commands[cmd]!==undefined )
            {
                if( Agocontrol.schema.commands[cmd].name!==undefined )
                {
                    var content = {};
                    content['id'] = cmd;
                    for( var item in  Agocontrol.schema.commands[cmd] )
                    {
                        content[item] = Agocontrol.schema.commands[cmd][item];
                    }
                    cmds[Agocontrol.schema.commands[cmd].name] = content;
                }
            }
        }
    }
    return cmds;
}

//====================================
//BLOCKS DEFINITION
//====================================

//device block
Blockly.Blocks['agocontrol_device'] = {
  init: function() {
    this.lastType = undefined;
    this.setHelpUrl('TODO');
    this.setColour(290);
    this.container = this.appendDummyInput()
        .appendField("device")
        .appendField(new Blockly.FieldDropdown(Agocontrol.getDeviceTypes), "TYPE")
        .appendField(new Blockly.FieldDropdown([['','']]), "DEVICE");
    this.setInputsInline(true);
    this.setOutput(true);
    this.setTooltip('TODO tooltip');
  },

  onchange: function() {
    if( !this.workspace )
        return;
    var currentType = this.getFieldValue("TYPE");
    if( this.lastType!=currentType )
    {
        this.lastType = currentType;
        var devices = Agocontrol.getDevices(currentType);
        if( devices.length==0 )
            devices.push(['','']);
        this.container.removeField("DEVICE");
        this.container.appendField(new Blockly.FieldDropdown(devices), "DEVICE");
    }
  },
};

//device event block
Blockly.Blocks['agocontrol_deviceEvents'] = {
  init: function() {
    this.lastType = undefined;
    this.setHelpUrl('TODO');
    this.setColour(290);
    this.container = this.appendDummyInput()
        .appendField("event")
        .appendField(new Blockly.FieldDropdown(Agocontrol.getDeviceTypes), "TYPE")
        .appendField(new Blockly.FieldDropdown([['','']]), "EVENT");
    this.setInputsInline(true);
    this.setOutput(true);
    this.setTooltip('TODO tooltip');
  },

  onchange: function() {
    if( !this.workspace )
        return;
    var currentType = this.getFieldValue("TYPE");
    if( this.lastType!=currentType )
    {
        this.lastType = currentType;
        var events = Agocontrol.getDeviceEvents(currentType);
        if( events.length==0 )
            events.push(['','']);
        this.container.removeField("EVENT");
        this.container.appendField(new Blockly.FieldDropdown(events), "EVENT");
    }
  },
};

//all events block
Blockly.Blocks['agocontrol_allEvents'] = {
  init: function() {
    this.setHelpUrl('TODO');
    this.setColour(290);
    this.container = this.appendDummyInput()
        .appendField("event")
        .appendField(new Blockly.FieldDropdown(Agocontrol.getAllEvents), "EVENT");
    this.setInputsInline(true);
    this.setOutput(true);
    this.setTooltip('TODO tooltip');
  }
};

//device property block
Blockly.Blocks['agocontrol_deviceProperties'] = {
  init: function() {
    //members
    this.properties = undefined;
    this.lastType = undefined;
    this.lastProp = undefined;
    this.customFields = [];

    //block definition
    this.setHelpUrl('TODO');
    this.setColour(290);
    this.container = this.appendDummyInput()
        .appendField("property")
        .appendField(new Blockly.FieldDropdown(Agocontrol.getDeviceTypes), "TYPE")
        .appendField(new Blockly.FieldDropdown([['','']]), "PROP");
    this.ccontainer = this.appendDummyInput();
    this._addCustomField("empty", new Blockly.FieldImage("/blockly/media/1x1.gif",1,1,""));
        //.appendField(new Blockly.FieldImage("/blockly/media/1x1.gif",1,1,""), "CUSTOM");
    this.setInputsInline(true);
    this.setOutput(true);
    this.setTooltip('TODO tooltip');
  },

  //add custom field (internal use)
  _addCustomField: function(key, field) {
    this.customFields.push(key);
    this.ccontainer.appendField(field, "CUSTOM_"+key);
  },

  //get block values
  getValues: function() {
    var values = {};
    for( var i=0; i<this.customFields.length; i++)
    {
        if( this.customFields[i]!='empty' && this.customFields[i]!='text' )
        {
            values[this.customFields[i]] = this.getFieldValue("CUSTOM_"+this.customFields[i]);
        }
    }
    return values;
  },

  //clear custom fields (internal use);
  _clearCustomFields: function() {
    for( var i=0; i<this.customFields.length; i++)
    {
        this.ccontainer.removeField("CUSTOM_"+this.customFields[i]);
    }
    while( this.customFields.length>0 )
    {
        this.customFields.pop();
    }
  },

  //onchange event
  onchange: function() {
    if( !this.workspace )
        return;

    //update properties list
    var currentType = this.getFieldValue("TYPE");
    if( this.lastType!=currentType )
    {
        this.lastType = currentType;
        this.properties = Agocontrol.getDeviceProperties(currentType);
        var props = [];
        for( var prop in this.properties )
        {
            props.push([prop, this.properties[prop].name]);
        }
        if( props.length==0 )
            props.push(['','']);
        this.container.removeField("PROP");
        this.container.appendField(new Blockly.FieldDropdown(props), "PROP");
    }

    //update block content according to selected type
    var currentProp = this.getFieldValue("PROP");
    if( this.lastProp!=currentProp )
    {
        this.lastProp = currentProp;
        //console.log('property='+currentProp);
        this._clearCustomFields();
        if( this.properties[currentProp]!==undefined )
        {
            switch(this.properties[currentProp].type)
            {
                //console.log(this.properties[currentProp]);
                case 'option':
                    var opts = [];
                    for( var i=0; i<this.properties[currentProp].options.length; i++ )
                    {
                        opts.push([this.properties[currentProp].options[i], this.properties[currentProp].options[i]]);
                    }
                    this._addCustomField("text", "=");
                    this._addCustomField("OPTION", new Blockly.FieldDropdown(opts));
                    break;
                case 'range':
                    this._addCustomField("text", "in range [");
                    this._addCustomField("MIN", new Blockly.FieldTextInput("0"));
                    this._addCustomField("text", ",");
                    this._addCustomField("MAX", new Blockly.FieldTextInput("100"));
                    this._addCustomField("text", "]");
                    break;
                case 'color':
                    this._addCustomField("text", "has colour");
                    this._addCustomField("COLOR", new Blockly.FieldColour("#000000"));
                    break;
                case 'time':
                    this._addCustomField("text", "is");
                    this._addCustomField("HOUR", new Blockly.FieldTextInput("0"));
                    this._addCustomField("text", ":");
                    this._addCustomField("MINUTE", new Blockly.FieldTextInput("0"));
                    break;
                case 'timeoffset':
                    this._addCustomField("text", "has offset");
                    this._addCustomField("SIGN", new Blockly.FieldDropdown([["-","minus"],["+","plus"]]));
                    this._addCustomField("HOUR", new Blockly.FieldTextInput("0"));
                    this._addCustomField("text", ":");
                    this._addCustomField("MINUTE", new Blockly.FieldTextInput("0"));
                    break;
                case 'threshold':
                    this._addCustomField("text", "is");
                    this._addCustomField("SIGN", new Blockly.FieldDropdown([[">","gt"],["<","lt"],[">=","gte"],["<=","lte"]]));
                    this._addCustomField("THRESHOLD", new Blockly.FieldTextInput("0"));
                case 'bool':
                    this._addCustomField("text", "is");
                    this._addCustomField("BOOL", new Blockly.FieldDropdown([["true","true"],["false","false"]]));
                default:
                    this._addCustomField("empty", new Blockly.FieldImage("/blockly/media/1x1.gif",1,1,""));
                    break;
            }
        }
        else
        {
            this._addCustomField("empty", new Blockly.FieldImage("/blockly/media/1x1.gif",1,1,""));
        }
    }
  }
};

Blockly.Blocks['agocontrol_deviceCommands'] = {
  init: function() {
    //members
    this.commands = undefined;
    this.lastType = undefined;
    this.lastCommand = undefined;
    this.customFields = [];

    //block definition
    this.setHelpUrl('TODO');
    this.setColour(290);
    this.container = this.appendDummyInput()
        .appendField("command")
        .appendField(new Blockly.FieldDropdown(Agocontrol.getDeviceTypes), "TYPE")
        .appendField(new Blockly.FieldDropdown([["option", "OPTIONNAME"]]), "CMD");
    //this.ccontainer = this.appendDummyInput()
    //    .appendField(new Blockly.FieldImage("/blockly/media/1x1.gif",1,1,""), "CUSTOM");
    //this.ccontainer = this.appendDummyInput();
    //this._addCustomField("empty", new Blockly.FieldImage("/blockly/media/1x1.gif",1,1,""));
    this.setPreviousStatement(true);
    this.setNextStatement(true);
    this.setTooltip('TODO tooltip');
  },

  //add custom field (internal use)
  _addCustomField: function(key, desc, field) {
    this.customFields.push(key);
    var input = this.appendDummyInput("INPUT_"+key);
    if( desc.length>0 )
        input.appendField('-'+desc);
    input.appendField(field, "CUSTOM_"+key);
  },

  //get block values
  getValues: function() {
    var values = {};
    for( var i=0; i<this.customFields.length; i++)
    {
        if( this.customFields[i]!='empty' && this.customFields[i]!='text' )
        {
            values[this.customFields[i]] = this.getFieldValue("CUSTOM_"+this.customFields[i]);
        }
    }
    return values;
  },

  //clear custom fields (internal use);
  _clearCustomFields: function() {
    for( var i=0; i<this.customFields.length; i++)
    {
        var input = this.getInput("INPUT_"+this.customFields[i]);
        if( input!==undefined )
        {
            //input.removeField("CUSTOM_"+this.customFields[i]);
            this.removeInput("INPUT_"+this.customFields[i]);
        }
        else
        {
            console.log("Unable to find INPUT_"+this.customFields[i]);
        }
    }
    while( this.customFields.length>0 )
    {
        this.customFields.pop();
    }
  },

  //onchange event
  onchange: function() {
    if( !this.workspace )
        return;

    //update commands list
    var currentType = this.getFieldValue("TYPE");
    if( this.lastType!=currentType )
    {
        this.lastType = currentType;
        this.commands = Agocontrol.getDeviceCommands(currentType);
        var cmds = [];
        for( var cmd in this.commands )
        {
            cmds.push([cmd, this.commands[cmd].name]);
        }
        if( cmds.length==0 )
            cmds.push(['','']);
        this.container.removeField("CMD");
        this.container.appendField(new Blockly.FieldDropdown(cmds), "CMD");
    }

    //update block content according to selected type
    var currentCmd = this.getFieldValue("CMD");
    if( this.lastCommand!=currentCmd )
    {
        this.lastCommand = currentCmd;
        this._clearCustomFields();
        //console.log(this.commands);
        if( this.commands[currentCmd]!==undefined && this.commands[currentCmd].parameters!==undefined )
        {
            for( var param in this.commands[currentCmd].parameters )
            {
                this._addCustomField(param.toUpperCase(), this.commands[currentCmd].parameters[param].name, new Blockly.FieldTextInput(""));
            }
        }
        else
        {
            this._addCustomField("empty", "", new Blockly.FieldImage("/blockly/media/1x1.gif",1,1,""));
        }
    }
  }
};

//window.addEventListener('load', init);

