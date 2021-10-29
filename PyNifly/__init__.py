"""NIF format export/import for Blender using Nifly"""

# Copyright © 2021, Bad Dog.


RUN_TESTS = False
TEST_BPY_ALL = True


bl_info = {
    "name": "NIF format",
    "description": "Nifly Import/Export for Skyrim, Skyrim SE, and Fallout 4 NIF files (*.nif)",
    "author": "Bad Dog",
    "blender": (2, 92, 0),
    "version": (1, 4, 4),  
    "location": "File > Import-Export",
    "warning": "WIP",
    "support": "COMMUNITY",
    "category": "Import-Export"
}

import sys
import os.path
import pathlib
import logging
from operator import or_
from functools import reduce
import traceback
import math
import re

log = logging.getLogger("pynifly")
log.info(f"Loading pynifly version {bl_info['version'][0]}.{bl_info['version'][1]}.{bl_info['version'][2]}")

pynifly_dev_root = r"C:\Users\User\OneDrive\Dev"
pynifly_dev_path = os.path.join(pynifly_dev_root, r"pynifly\pynifly")
nifly_path = os.path.join(pynifly_dev_root, r"PyNifly\NiflyDLL\x64\Debug\NiflyDLL.dll")

if os.path.exists(nifly_path):
    log.debug(f"PyNifly dev path: {pynifly_dev_path}")
    if pynifly_dev_path not in sys.path:
        sys.path.append(pynifly_dev_path)
    if RUN_TESTS:
        log.setLevel(logging.DEBUG)
    else:
        log.setLevel(logging.INFO)
else:
    # Load from install location
    py_addon_path = os.path.dirname(os.path.realpath(__file__))
    log.debug(f"PyNifly addon path: {py_addon_path}")
    if py_addon_path not in sys.path:
        sys.path.append(py_addon_path)
    nifly_path = os.path.join(py_addon_path, "NiflyDLL.dll")
    log.setLevel(logging.INFO)

log.info(f"Nifly DLL at {nifly_path}")
if not os.path.exists(nifly_path):
    log.error("ERROR: pynifly DLL not found")

from pynifly import *
from niflytools import *
from trihandler import *
import pyniflywhereami

import bpy
import bpy_types
from bpy.props import (
        BoolProperty,
        FloatProperty,
        StringProperty,
        EnumProperty,
        )
from bpy_extras.io_utils import (
        ImportHelper,
        ExportHelper)
import bmesh

NO_PARTITION_GROUP = "*NO_PARTITIONS*"
MULTIPLE_PARTITION_GROUP = "*MULTIPLE_PARTITIONS*"
UNWEIGHTED_VERTEX_GROUP = "*UNWEIGHTED_VERTICES*"
ALPHA_MAP_NAME = "VERTEX_ALPHA"

GLOSS_SCALE = 100

z180 = MatTransform((0, 0, 0), 
                    [(-1, 0, 0), 
                     (0, -1, 0),
                     (0, 0, 1)],
                     1)

#log.setLevel(logging.DEBUG)
#pynifly_ch = logging.StreamHandler()
#pynifly_ch.setLevel(logging.DEBUG)
#formatter = logging.Formatter('%(name)s-%(levelname)s: %(message)s')
#pynifly_ch.setFormatter(formatter)
#log.addHandler(ch)


# ######################################################################## ###
#                                                                          ###
# -------------------------------- IMPORT -------------------------------- ###
#                                                                          ###
# ######################################################################## ###

# -----------------------------  EXTRA DATA  -------------------------------

def import_extra(f: NifFile):
    """ Import any extra data from the root, and create corresponding shapes 
        Returns a list of the new extradata objects
    """
    extradata = []
    loc = [0.0, 0.0, 0.0]

    for s in f.string_data:
        bpy.ops.object.add(radius=1.0, type='EMPTY', location=loc)
        ed = bpy.context.object
        ed.name = "NiStringExtraData"
        ed.show_name = True
        ed['NiStringExtraData_Name'] = s[0]
        ed['NiStringExtraData_Value'] = s[1]
        loc[0] += 3.0
        extradata.append(ed)

    for s in f.behavior_graph_data:
        bpy.ops.object.add(radius=1.0, type='EMPTY', location=loc)
        ed = bpy.context.object
        ed.name = "BSBehaviorGraphExtraData"
        ed.show_name = True
        ed['BSBehaviorGraphExtraData_Name'] = s[0]
        ed['BSBehaviorGraphExtraData_Value'] = s[1]
        loc[0] += 3.0
        extradata.append(ed)

    return extradata


def import_shape_extra(obj, shape):
    """ Import any extra data from the shape if given or the root if not, and create 
    corresponding shapes """
    extradata = []
    loc = list(obj.location)

    for s in shape.string_data:
        bpy.ops.object.add(radius=1.0, type='EMPTY', location=loc)
        ed = bpy.context.object
        ed.name = "NiStringExtraData"
        ed.show_name = True
        ed['NiStringExtraData_Name'] = s[0]
        ed['NiStringExtraData_Value'] = s[1]
        ed.parent = obj
        loc[0] += 3.0
        extradata.append(ed)

    for s in shape.behavior_graph_data:
        bpy.ops.object.add(radius=1.0, type='EMPTY', location=loc)
        ed = bpy.context.object
        ed.name = "BSBehaviorGraphExtraData"
        ed.show_name = True
        ed['BSBehaviorGraphExtraData_Name'] = s[0]
        ed['BSBehaviorGraphExtraData_Value'] = s[1]
        ed.parent = obj
        loc[0] += 3.0
        extradata.append(ed)

    return extradata


def export_shape_data(obj, shape):
    ed = [ (x['NiStringExtraData_Name'], x['NiStringExtraData_Value']) for x in \
            obj.children if 'NiStringExtraData_Name' in x.keys()]
    if len(ed) > 0:
        shape.string_data = ed
    
    ed = [ (x['BSBehaviorGraphExtraData_Name'], x['BSBehaviorGraphExtraData_Value']) for x in \
            obj.children if 'BSBehaviorGraphExtraData_Name' in x.keys()]
    if len(ed) > 0:
        shape.behavior_graph_data = ed


# -----------------------------  SHADERS  -------------------------------

def get_image_node(node_input):
    """Walk the shader nodes backwards until a texture node is found.
        node_input = the shader node input to follow; may be null"""
    n = None
    if node_input and len(node_input.links) > 0: 
        n = node_input.links[0].from_node

    while n and type(n) != bpy.types.ShaderNodeTexImage:
        if 'Base Color' in n.inputs.keys() and n.inputs['Base Color'].is_linked:
            n = n.inputs['Base Color'].links[0].from_node
        elif 'Image' in n.inputs.keys() and n.inputs['Image'].is_linked:
            n = n.inputs['Image'].links[0].from_node
        elif 'Color' in n.inputs.keys() and n.inputs['Color'].is_linked:
            n = n.inputs['Color'].links[0].from_node
        elif 'R' in n.inputs.keys() and n.inputs['R'].is_linked:
            n = n.inputs['R'].links[0].from_node
    return n

def find_shader_node(nodelist, idname):
    return next((x for x in nodelist if x.bl_idname == idname), None)

def import_shader_attrs(material, shader, shape):
    attrs = shape.shader_attributes
    if not attrs: 
        return

    try:
        material['BSLSP_Shader_Type'] = attrs.Shader_Type
        material['BSLSP_Shader_Name'] = shape.shader_name
        material['BSLSP_Shader_Flags_1'] = hex(attrs.Shader_Flags_1)
        material['BSLSP_Shader_Flags_2'] = hex(attrs.Shader_Flags_2)
        shader.inputs['Emission'].default_value = (attrs.Emissive_Color_R, attrs.Emissive_Color_G, attrs.Emissive_Color_B, attrs.Emissive_Color_A)
        shader.inputs['Emission Strength'].default_value = attrs.Emissive_Mult
        shader.inputs['Alpha'].default_value = attrs.Alpha
        material['BSLSP_Refraction_Str'] = attrs.Refraction_Str
        shader.inputs['Metallic'].default_value = attrs.Glossiness/GLOSS_SCALE
        material['BSLSP_Spec_Color_R'] = attrs.Spec_Color_R
        material['BSLSP_Spec_Color_G'] = attrs.Spec_Color_G
        material['BSLSP_Spec_Color_B'] = attrs.Spec_Color_B
        material['BSLSP_Spec_Str'] = attrs.Spec_Str
        material['BSLSP_Soft_Lighting'] = attrs.Soft_Lighting
        material['BSLSP_Rim_Light_Power'] = attrs.Rim_Light_Power
        material['BSLSP_Skin_Tint_Color_R'] = attrs.Skin_Tint_Color_R
        material['BSLSP_Skin_Tint_Color_G'] = attrs.Skin_Tint_Color_G
        material['BSLSP_Skin_Tint_Color_B'] = attrs.Skin_Tint_Color_B
    except Exception as e:
        # Any errors, print the error but continue
        log.warning(str(e))

def import_shader_alpha(mat, shape):
    if shape.has_alpha_property:
        mat.alpha_threshold = shape.alpha_property.threshold
        if shape.alpha_property.flags & 1:
            mat.blend_method = 'BLEND'
            mat.alpha_threshold = shape.alpha_property.threshold/255
        else:
            mat.blend_method = 'CLIP'
            mat.alpha_threshold = shape.alpha_property.threshold/255
        mat['NiAlphaProperty_flags'] = shape.alpha_property.flags
        mat['NiAlphaProperty_threshold'] = shape.alpha_property.threshold
        return True
    else:
        return False

def obj_create_material(obj, shape):
    img_offset_x = -1200
    cvt_offset_x = -300
    inter1_offset_x = -900
    inter2_offset_x = -700
    inter3_offset_x = -500
    offset_y = -300
    yloc = 0

    nifpath = shape.parent.filepath

    fulltextures = extend_filenames(nifpath, "meshes", shape.textures)
    missing = missing_files(fulltextures)
    if len(missing) > 0:
        log.warning(f". . Some texture files not found: {missing}")
    #if not check_files(fulltextures):
    #    log.debug(f". . texture files not available, not creating material: \n\tnif path = {nifpath}\n\t textures = {fulltextures}")
    #    return
    log.debug(". . creating material")

    mat = bpy.data.materials.new(name=(obj.name + ".Mat"))

    # Stash texture strings for future export
    for i, t in enumerate(shape.textures):
        mat['BSShaderTextureSet_' + str(i)] = t

    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    bdsf = nodes.get("Principled BSDF")

    import_shader_attrs(mat, bdsf, shape)
    has_alpha = import_shader_alpha(mat, shape)

    # --- Diffuse --

    txtnode = nodes.new("ShaderNodeTexImage")
    try:
        img = bpy.data.images.load(fulltextures[0], check_existing=True)
        img.colorspace_settings.name = "sRGB"
        txtnode.image = img
    except:
        pass
    txtnode.location = (bdsf.location[0] + img_offset_x, bdsf.location[1])
    
    mat.node_tree.links.new(txtnode.outputs['Color'], bdsf.inputs['Base Color'])
    if has_alpha:
        mat.node_tree.links.new(txtnode.outputs['Alpha'], bdsf.inputs['Alpha'])

    yloc = txtnode.location[1] + offset_y

    matlinks = mat.node_tree.links

    # --- Subsurface --- 

    if fulltextures[2] != "": 
        # Have a sk separate from a specular
        skimgnode = nodes.new("ShaderNodeTexImage")
        try:
            skimg = bpy.data.images.load(fulltextures[2], check_existing=True)
            if skimg != txtnode.image:
                skimg.colorspace_settings.name = "Non-Color"
            skimgnode.image = skimg
        except:
            pass
        skimgnode.location = (txtnode.location[0], yloc)
        matlinks.new(skimgnode.outputs['Color'], bdsf.inputs["Subsurface Color"])
        yloc = skimgnode.location[1] + offset_y
        
    # --- Specular --- 

    if fulltextures[7] != "":
        simgnode = nodes.new("ShaderNodeTexImage")
        try:
            simg = bpy.data.images.load(fulltextures[7], check_existing=True)
            simg.colorspace_settings.name = "Non-Color"
            simgnode.image = simg
        except:
            pass
        simgnode.location = (txtnode.location[0], yloc)

        if shape.parent.game in ["FO4"]:
            # specular combines gloss and spec
            seprgb = nodes.new("ShaderNodeSeparateRGB")
            seprgb.location = (bdsf.location[0] + cvt_offset_x, yloc)
            matlinks.new(simgnode.outputs['Color'], seprgb.inputs['Image'])
            matlinks.new(seprgb.outputs['R'], bdsf.inputs['Specular'])
            matlinks.new(seprgb.outputs['G'], bdsf.inputs['Metallic'])
        else:
            matlinks.new(simgnode.outputs['Color'], bdsf.inputs['Specular'])
            # bdsf.inputs['Metallic'].default_value = 0
            
        yloc = simgnode.location[1] + offset_y

    # --- Normal Map --- 
    
    if fulltextures[1] != "":
        nmap = nodes.new("ShaderNodeNormalMap")
        if shape.shader_attributes and shape.shader_attributes.shaderflags1_test(ShaderFlags1.MODEL_SPACE_NORMALS):
            nmap.space = "OBJECT"
        else:
            nmap.space = "TANGENT"
        nmap.location = (bdsf.location[0] + cvt_offset_x, yloc)
        
        nimgnode = nodes.new("ShaderNodeTexImage")
        try:
            nimg = bpy.data.images.load(fulltextures[1], check_existing=True) 
            nimg.colorspace_settings.name = "Non-Color"
            nimgnode.image = nimg
        except:
            pass
        nimgnode.location = (txtnode.location[0], yloc)
        
        if shape.shader_attributes.shaderflags1_test(ShaderFlags1.MODEL_SPACE_NORMALS):
            # Need to swap green and blue channels for blender
            rgbsep = nodes.new("ShaderNodeSeparateRGB")
            rgbcomb = nodes.new("ShaderNodeCombineRGB")
            matlinks.new(rgbsep.outputs['R'], rgbcomb.inputs['R'])
            matlinks.new(rgbsep.outputs['G'], rgbcomb.inputs['B'])
            matlinks.new(rgbsep.outputs['B'], rgbcomb.inputs['G'])
            matlinks.new(rgbcomb.outputs['Image'], nmap.inputs['Color'])
            matlinks.new(nimgnode.outputs['Color'], rgbsep.inputs['Image'])
            rgbsep.location = (bdsf.location[0] + inter1_offset_x, yloc)
            rgbcomb.location = (bdsf.location[0] + inter2_offset_x, yloc)
        elif shape.parent.game == 'FO4':
            # Need to invert the green channel for blender
            rgbsep = nodes.new("ShaderNodeSeparateRGB")
            rgbcomb = nodes.new("ShaderNodeCombineRGB")
            colorinv = nodes.new("ShaderNodeInvert")
            matlinks.new(rgbsep.outputs['R'], rgbcomb.inputs['R'])
            matlinks.new(rgbsep.outputs['B'], rgbcomb.inputs['B'])
            matlinks.new(rgbsep.outputs['G'], colorinv.inputs['Color'])
            matlinks.new(colorinv.outputs['Color'], rgbcomb.inputs['G'])
            matlinks.new(rgbcomb.outputs['Image'], nmap.inputs['Color'])
            matlinks.new(nimgnode.outputs['Color'], rgbsep.inputs['Image'])
            rgbsep.location = (bdsf.location[0] + inter1_offset_x, yloc)
            rgbcomb.location = (bdsf.location[0] + inter3_offset_x, yloc)
            colorinv.location = (bdsf.location[0] + inter2_offset_x, yloc - rgbcomb.height * 0.9)
        else:
            matlinks.new(nimgnode.outputs['Color'], nmap.inputs['Color'])
            nmap.location = (bdsf.location[0] + inter2_offset_x, yloc)
                         
        matlinks.new(nmap.outputs['Normal'], bdsf.inputs['Normal'])

        if shape.parent.game in ["SKYRIM", "SKYRIMSE"] and \
            shape.shader_attributes and \
            not shape.shader_attributes.shaderflags1_test(ShaderFlags1.MODEL_SPACE_NORMALS):
            # Specular is in the normal map alpha channel
            matlinks.new(nimgnode.outputs['Alpha'], bdsf.inputs['Specular'])
            
        
    obj.active_material = mat

def export_shader_attrs(obj, shader, shape):
    mat = obj.active_material

    if 'BSLSP_Shader_Type' in mat.keys():
        shape.shader_attributes.Shader_Type = int(mat['BSLSP_Shader_Type'])
        log.debug(f"....setting shader type to {shape.shader_attributes.Shader_Type}")
    if 'BSLSP_Shader_Name' in mat.keys() and len(mat['BSLSP_Shader_Name']) > 0:
        shape.shader_name = mat['BSLSP_Shader_Name']
    if 'BSLSP_Shader_Flags_1' in mat.keys():
        shape.shader_attributes.Shader_Flags_1 = int(mat['BSLSP_Shader_Flags_1'], 16)
    if 'BSLSP_Shader_Flags_2' in mat.keys():
        shape.shader_attributes.Shader_Flags_2 = int(mat['BSLSP_Shader_Flags_2'], 16)
    shape.shader_attributes.Emissive_Color_R = shader.inputs['Emission'].default_value[0]
    shape.shader_attributes.Emissive_Color_G = shader.inputs['Emission'].default_value[1]
    shape.shader_attributes.Emissive_Color_B = shader.inputs['Emission'].default_value[2]
    shape.shader_attributes.Emissive_Color_A = shader.inputs['Emission'].default_value[3]
    shape.shader_attributes.Emissive_Mult = shader.inputs['Emission Strength'].default_value
    shape.shader_attributes.Alpha = shader.inputs['Alpha'].default_value
    if 'BSLSP_Refraction_Str' in mat.keys():
        shape.Refraction_Str = mat['BSLSP_Refraction_Str']
    shape.shader_attributes.Glossiness = shader.inputs['Metallic'].default_value * GLOSS_SCALE
    if 'BSLSP_Spec_Color_R' in mat.keys():
        shape.shader_attributes.Spec_Color_R = mat['BSLSP_Spec_Color_R']
    if 'BSLSP_Spec_Color_G' in mat.keys():
        shape.shader_attributes.Spec_Color_G = mat['BSLSP_Spec_Color_G']
    if 'BSLSP_Spec_Color_B' in mat.keys():
        shape.shader_attributes.Spec_Color_B = mat['BSLSP_Spec_Color_B']
    if 'BSLSP_Spec_Str' in mat.keys():
        shape.shader_attributes.Spec_Str = mat['BSLSP_Spec_Str']
    if 'BSLSP_Spec_Str' in mat.keys():
        shape.shader_attributes.Soft_Lighting = mat['BSLSP_Soft_Lighting']
    if 'BSLSP_Spec_Str' in mat.keys():
        shape.shader_attributes.Rim_Light_Power = mat['BSLSP_Rim_Light_Power']
    if 'BSLSP_Skin_Tint_Color_R' in mat.keys():
        shape.shader_attributes.Skin_Tint_Color_R = mat['BSLSP_Skin_Tint_Color_R']
    if 'BSLSP_Skin_Tint_Color_G' in mat.keys():
        shape.shader_attributes.Skin_Tint_Color_G = mat['BSLSP_Skin_Tint_Color_G']
    if 'BSLSP_Skin_Tint_Color_B' in mat.keys():
        shape.shader_attributes.Skin_Tint_Color_G = mat['BSLSP_Skin_Tint_Color_B']

    #log.debug(f"Shader Type: {shape.shader_attributes.Shader_Type}")
    #log.debug(f"Shader attributes: \n{shape.shader_attributes}")

def has_msn_shader(obj):
    val = False
    if obj.active_material:
        nodelist = obj.active_material.node_tree.nodes
        shader_node = find_shader_node(nodelist, 'ShaderNodeBsdfPrincipled')
        normal_input = shader_node.inputs['Normal']
        if normal_input and normal_input.is_linked:
            nmap_node = normal_input.links[0].from_node
            if nmap_node.bl_idname == 'ShaderNodeNormalMap' and nmap_node.space == "OBJECT":
                val = True
    return val


def read_object_texture(mat: bpy.types.Material, index: int):
    """Return the index'th texture in the saved texture custom properties"""
    n = 'BSShaderTextureSet_' + str(index)
    try:
        return mat[n]
    except:
        return None


def set_object_texture(shape: NiShape, mat: bpy.types.Material, i: int):
    t = read_object_texture(mat, i)
    if t:
        shape.set_texture(i, t)


def export_shader(obj, shape: NiShape):
    """Create shader from the object's material"""
    log.debug(f"...exporting material for object {obj.name}")
    shader = shape.shader_attributes
    mat = obj.active_material
    nodelist = mat.node_tree.nodes

    shader_node = None
    diffuse_fp = None
    norm_fp = None
    sk_fp = None
    spec_fp = None

    if not 'Principled BSDF' in nodelist:
        log.warning(f"...Have material but no Principled BSDF for {obj.name}")
    else:
        shader_node = nodelist['Principled BSDF']

    for i in [3, 4, 5, 6, 8]:
        set_object_texture(shape, mat, i)
    
    # Texture paths
    norm_txt_node = None
    if shader_node:
        export_shader_attrs(obj, shader_node, shape)

        diffuse_input = shader_node.inputs['Base Color']
        if diffuse_input and diffuse_input.is_linked:
            diffuse_node = diffuse_input.links[0].from_node
            if diffuse_node.image:
                diffuse_fp_full = diffuse_node.image.filepath
                diffuse_fp = diffuse_fp_full[diffuse_fp_full.lower().find('textures'):]
                log.debug(f"....Writing diffuse texture path '{diffuse_fp}'")
        
        normal_input = shader_node.inputs['Normal']
        is_obj_space = False
        if normal_input and normal_input.is_linked:
            nmap_node = normal_input.links[0].from_node
            if nmap_node.bl_idname == 'ShaderNodeNormalMap':
                is_obj_space = (nmap_node.space == "OBJECT")
                if is_obj_space:
                    shape.shader_attributes.shaderflags1_set(ShaderFlags1.MODEL_SPACE_NORMALS)
                else:
                    shape.shader_attributes.shaderflags1_clear(ShaderFlags1.MODEL_SPACE_NORMALS)
                image_node = get_image_node(nmap_node.inputs['Color'])
                if image_node and image_node.image:
                    norm_txt_node = image_node
                    norm_fp_full = norm_txt_node.image.filepath
                    norm_fp = norm_fp_full[norm_fp_full.lower().find('textures'):]
                    log.debug(f"....Writing normal texture path '{norm_fp}'")

        sk_node = get_image_node(shader_node.inputs['Subsurface Color'])
        if sk_node and sk_node.image:
            sk_fp_full = sk_node.image.filepath
            sk_fp = sk_fp_full[sk_fp_full.lower().find('textures'):]
            log.debug(f"....Writing subsurface texture path '{sk_fp}'")

        # Separate specular slot is only used if it's a MSN
        if is_obj_space:
            spec_node = get_image_node(shader_node.inputs['Specular'])
            if spec_node and spec_node.image:
                    spec_fp_full = spec_node.image.filepath
                    spec_fp = spec_fp_full[spec_fp_full.lower().find('textures'):]
                    log.debug(f"....Writing subsurface texture path '{spec_fp}'")

        alpha_input = shader_node.inputs['Alpha']
        if alpha_input and alpha_input.is_linked:
            mat = obj.active_material
            if 'NiAlphaProperty_flags' in mat.keys():
                shape.alpha_property.flags = mat['NiAlphaProperty_flags']
            else:
                shape.alpha_property.flags = 4844
            shape.alpha_property.threshold = int(mat.alpha_threshold * 255)
            #if 'NiAlphaProperty_threshold' in mat.keys():
            #    shape.alpha_property.threshold = mat['NiAlphaProperty_threshold']
            #else:
            #    shape.alpha_property.threshold = 128
            shape.save_alpha_property()

    else:
        log.warning(f"...Have material but no shader node for {obj.name}")

    if diffuse_fp:
        shape.set_texture(0, diffuse_fp)
    else:
        set_object_texture(shape, mat, 0)
    if norm_fp:
        shape.set_texture(1, norm_fp)
    else:
        set_object_texture(shape, mat, 1)
    if sk_fp:
        shape.set_texture(2, sk_fp)
    else:
        set_object_texture(shape, mat, 2)
    if spec_fp:
        shape.set_texture(7, spec_fp)
    else:
        set_object_texture(shape, mat, 7)


# -----------------------------  MESH CREATION -------------------------------

def mesh_create_uv(the_mesh, uv_points):
    """ Create UV in Blender to match UVpoints from Nif
        uv_points = [(u, v)...] indexed by vertex index
        """
    new_uv = [(0,0)] * len(the_mesh.loops)
    for lp_idx, lp in enumerate(the_mesh.loops):
        vert_targeted = lp.vertex_index
        new_uv[lp_idx] = (uv_points[vert_targeted][0], 1-uv_points[vert_targeted][1])
    new_uvlayer = the_mesh.uv_layers.new(do_init=False)
    for i, this_uv in enumerate(new_uv):
        new_uvlayer.data[i].uv = this_uv

def mesh_create_bone_groups(the_shape, the_object, do_name_xlate):
    """ Create groups to capture bone weights """
    vg = the_object.vertex_groups
    for bone_name in the_shape.bone_names:
        if do_name_xlate:
            xlate_name = the_shape.parent.blender_name(bone_name)
        else:
            xlate_name = bone_name
        new_vg = vg.new(name=xlate_name)
        for v, w in the_shape.bone_weights[bone_name]:
            new_vg.add((v,), w, 'ADD')
    

def mesh_create_partition_groups(the_shape, the_object):
    """ Create groups to capture partitions """
    mesh = the_object.data
    vg = the_object.vertex_groups
    partn_groups = []
    for p in the_shape.partitions:
        log.debug(f"..found partition {p.name}")
        new_vg = vg.new(name=p.name)
        partn_groups.append(new_vg)
        if type(p) == FO4Segment:
            for sseg in p.subsegments:
                log.debug(f"..found subsegment {sseg.name}")
                new_vg = vg.new(name=sseg.name)
                partn_groups.append(new_vg)
    for part_idx, face in zip(the_shape.partition_tris, mesh.polygons):
        if part_idx < len(partn_groups):
            this_vg = partn_groups[part_idx]
            for lp in face.loop_indices:
                this_loop = mesh.loops[lp]
                this_vg.add((this_loop.vertex_index,), 1.0, 'ADD')
    if len(the_shape.segment_file) > 0:
        log.debug(f"..Putting segment file '{the_shape.segment_file}' on '{the_object.name}'")
        the_object['FO4_SEGMENT_FILE'] = the_shape.segment_file


def import_colors(mesh, shape):
    if len(shape.colors) > 0:
        log.debug(f"..Importing vertex colors for {shape.name}")
        clayer = mesh.vertex_colors.new()
        alphlayer = mesh.vertex_colors.new()
        alphlayer.name = ALPHA_MAP_NAME
        
        colors = shape.colors
        for lp in mesh.loops:
            c = colors[lp.vertex_index]
            clayer.data[lp.index].color = (c[0], c[1], c[2], 1.0)
            alph = colors[lp.vertex_index][3]
            alphlayer.data[lp.index].color = [alph, alph, alph, 1.0]


class NifImporter():
    """Does the work of importing a nif, independent of Blender's operator interface"""
    class ImportFlags(IntFlag):
        CREATE_BONES = 1
        RENAME_BONES = 1 << 1
        ROTATE_MODEL = 1 << 2

    def __init__(self, 
                 filename: str, 
                 f: ImportFlags = ImportFlags.CREATE_BONES | ImportFlags.RENAME_BONES):
        self.filename = filename
        self.flags = f
        self.armature = None
        self.bones = set()
        self.objects_created = []
        self.nif = NifFile(filename)

    def import_shape(self, the_shape: NiShape):
        """ Import the shape to a Blender object, translating bone names 
            self.objects_created = Set to a list of objects created. Might be more than one because 
                of extra data nodes.
        """
        log.debug(f". Importing shape {the_shape.name}")
        v = the_shape.verts
        t = the_shape.tris

        new_mesh = bpy.data.meshes.new(the_shape.name)
        new_mesh.from_pydata(v, [], t)
        new_object = bpy.data.objects.new(the_shape.name, new_mesh)
        self.objects_created.append(new_object)
    
        import_colors(new_mesh, the_shape)

        log.info(f". . import flags: {self.flags}")
        if not the_shape.has_skin_instance:
            # Statics get transformed according to the shape's transform
            #new_object.scale = (the_shape.transform.scale, ) * 3
            # xf = the_shape.transform.invert()
            # new_object.matrix_world = xf.as_matrix() 
            # new_object.location = the_shape.transform.translation
            log.debug(f". . shape {the_shape.name} transform: {the_shape.transform}")
            new_object.matrix_world = the_shape.transform.invert().as_matrix()
            new_object.location = the_shape.transform.translation
            new_object.scale = [the_shape.transform.scale] * 3
            log.debug(f". . New object transform: \n{new_object.matrix_world}")
        else:
            # Global-to-skin transform is what offsets all the vertices together, e.g. so that
            # heads can be positioned at the origin. Put the reverse transform on the blender 
            # object so they can be worked on in their skinned position.
            # Use the one on the NiSkinData if it exists.
            xform = the_shape.global_to_skin_data
            if xform is None:
                xform = the_shape.global_to_skin
            log.debug(f"....Found transform {the_shape.global_to_skin} on {the_shape.name} in '{self.nif.filepath}'")
            inv_xf = xform.invert()
            new_object.matrix_world = inv_xf.as_matrix()
            new_object.location = inv_xf.translation
            #new_object.scale = [inv_xf.scale] * 3
            #new_object.location = inv_xf.translation
            # vv Use matrix here instead of conversion?
            # And why does this work? Shouldn't this be in radians?
            #new_object.rotation_euler = inv_xf.rotation.euler_deg()
            #new_object.rotation_euler = inv_xf.rotation.euler()
            log.debug(f"..Object {new_object.name} created at {new_object.location[:]}")

        if self.flags & self.ImportFlags.ROTATE_MODEL:
            log.info(f". . Rotating model to match blender")
            r = new_object.rotation_euler[:]
            new_object.rotation_euler = (r[0], r[1], r[2]+pi)
            new_object["PYNIFLY_IS_ROTATED"] = True

        mesh_create_uv(new_object.data, the_shape.uvs)
        mesh_create_bone_groups(the_shape, new_object, self.flags & self.ImportFlags.RENAME_BONES)
        mesh_create_partition_groups(the_shape, new_object)
        for f in new_mesh.polygons:
            f.use_smooth = True

        new_mesh.update(calc_edges=True, calc_edges_loose=True)
        new_mesh.validate(verbose=True)

        obj_create_material(new_object, the_shape)

        self.objects_created.extend(import_shape_extra(new_object, the_shape))


    def add_bone_to_arma(self, name):
        """ Add bone to armature. Bone may come from nif or reference skeleton.
            name = name to use for the bone in blender 
            returns new bone
        """
        armdata = self.armature.data

        if name in armdata.edit_bones:
            return None
    
        # use the transform in the file if there is one; otherwise get the 
        # transform from the reference skeleton
        bone_xform = self.nif.get_node_xform_to_global(self.nif.nif_name(name)) 

        bone = armdata.edit_bones.new(name)
        bone.head = bone_xform.translation
        if self.nif.game in ("SKYRIM", "SKYRIMSE"):
            rot_vec = bone_xform.rotation.by_vector((0.0, 0.0, 5.0))
        else:
            rot_vec = bone_xform.rotation.by_vector((5.0, 0.0, 0.0))
        bone.tail = (bone.head[0] + rot_vec[0], bone.head[1] + rot_vec[1], bone.head[2] + rot_vec[2])
        bone['pyxform'] = bone_xform.rotation.matrix # stash for later

        return bone


    def connect_armature(self):
        """ Connect up the bones in an armature to make a full skeleton.
            Use parent/child relationships in the nif if present, from the skel otherwise.
            Uses flags
                CREATE_BONES - add bones from skeleton as needed
                RENAME_BONES - rename bones to conform with blender conventions
            """
        arm_data = self.armature.data
        bones_to_parent = [b.name for b in arm_data.edit_bones]

        i = 0
        while i < len(bones_to_parent): # list will grow while iterating
            bonename = bones_to_parent[i]
            arma_bone = arm_data.edit_bones[bonename]

            if arma_bone.parent is None:
                parentname = None
                skelbone = None
                # look for a parent in the nif
                nifname = self.nif.nif_name(bonename)
                if nifname in self.nif.nodes:
                    niparent = self.nif.nodes[nifname].parent
                    if niparent and niparent._handle != self.nif.root:
                        if self.flags & self.ImportFlags.RENAME_BONES:
                            parentname = niparent.blender_name
                        else:
                            parentname = niparent.nif_name

                if parentname is None and (self.flags & self.ImportFlags.CREATE_BONES):
                    # No parent in the nif. If it's a known bone, get parent from skeleton
                    if self.flags & self.ImportFlags.RENAME_BONES:
                        if arma_bone.name in self.nif.dict.byBlender:
                            p = self.nif.dict.byBlender[bonename].parent
                            if p:
                                parentname = p.blender
                    else:
                        if arma_bone.name in self.nif.dict.byNif:
                            p = self.nif.dict.byNif[bonename].parent
                            if p:
                                parentname = p.nif
            
                # if we got a parent from somewhere, hook it up
                if parentname:
                    if parentname not in arm_data.edit_bones:
                        # Add parent bones and put on our list so we can get its parent
                        new_parent = self.add_bone_to_arma(parentname)
                        bones_to_parent.append(parentname)  
                        arma_bone.parent = new_parent
                    else:
                        arma_bone.parent = arm_data.edit_bones[parentname]
            i += 1
        

    def make_armature(self,
                      the_coll: bpy_types.Collection, 
                      bone_names: set):
        """ Make a Blender armature from the given info. If the current active object is an
                armature, bones will be added to it instead of creating a new one.
            Inputs:
                the_coll = Collection to put the armature in. 
                bone_names = bones to include in the armature. Additional bones will be added from
                    the reference skeleton as needed to connect every bone to the skeleton root.
                self.armature = existing armature to add the new bones to. May be None.
            Returns: 
                self.armature = new armature, set as active object
            """
        if self.armature is None:
            log.debug(f"..Creating new armature for the import")
            arm_data = bpy.data.armatures.new(self.nif.rootName)
            self.armature = bpy.data.objects.new(self.nif.rootName, arm_data)
            the_coll.objects.link(self.armature)
        else:
            self.armature = self.armature

        bpy.ops.object.select_all(action='DESELECT')
        self.armature.select_set(True)
        bpy.context.view_layer.objects.active = self.armature
        bpy.ops.object.mode_set(mode='OBJECT', toggle=False)
        bpy.ops.object.mode_set(mode='EDIT', toggle=False)
    
        for bone_game_name in bone_names:
            if self.flags & self.ImportFlags.RENAME_BONES:
                name = self.nif.blender_name(bone_game_name)
            else:
                name = bone_game_name
            self.add_bone_to_arma(name)
        
        # Hook the armature bones up to a skeleton
        self.connect_armature()

        bpy.ops.object.mode_set(mode='OBJECT', toggle=False)


    def execute(self):
        """Perform the import operation as previously defined"""
        NifFile.clear_log()

        new_collection = bpy.data.collections.new(os.path.basename(self.filename))
        bpy.context.scene.collection.children.link(new_collection)
        bpy.context.view_layer.active_layer_collection \
             = bpy.context.view_layer.layer_collection.children[new_collection.name]
    
        log.info(f"Importing {self.nif.game} file {self.nif.filepath}")
        if bpy.context.object and bpy.context.object.type == "ARMATURE":
            self.armature = bpy.context.object
            log.info(f"..Current object is an armature, parenting shapes to {self.armature.name}")

        # Import shapes
        for s in self.nif.shapes:
            for n in s.bone_names: 
                #log.debug(f"....adding bone {n} for {s.name}")
                self.bones.add(n) 
            if self.nif.game == 'FO4' and fo4FaceDict.matches(self.bones) > 10:
                self.nif.dict = fo4FaceDict

            self.import_shape(s)

        for obj in self.objects_created:
            if not obj.name in new_collection.objects and obj.type == 'MESH':
                log.debug(f"...Adding object {obj.name} to collection {new_collection.name}")
                new_collection.objects.link(obj)

        # Import armature
        if len(self.bones) > 0 or len(self.nif.shapes) == 0:
            if len(self.nif.shapes) == 0:
                log.debug(f"....No shapes in nif, importing bones as skeleton")
                self.bones = set(self.nif.nodes.keys())
            else:
                log.debug(f"....Found self.bones, creating armature")
            self.make_armature(new_collection, self.bones)
        
            if len(self.objects_created) > 0:
                for o in self.objects_created: 
                    if o.type == 'MESH': 
                        o.select_set(True)
                bpy.ops.object.parent_set(type='ARMATURE_NAME', xmirror=False, keep_transform=False)
            else:
                self.armature.select_set(True)
    
        # Import nif-level extra data
        objs = import_extra(self.nif)
        #for o in objs:
        #    new_collection.objects.link(o)

        for o in self.objects_created: o.select_set(True)
        if len(self.objects_created) > 0:
            bpy.context.view_layer.objects.active = self.objects_created[0]


    @classmethod
    def do_import(cls, 
                  filename: str, 
                  flags: ImportFlags = ImportFlags.CREATE_BONES | ImportFlags.RENAME_BONES):
        imp = NifImporter(filename, flags)
        imp.execute()
        return imp


class ImportNIF(bpy.types.Operator, ImportHelper):
    """Load a NIF File"""
    bl_idname = "import_scene.nifly"
    bl_label = "Import NIF (Nifly)"
    bl_options = {'PRESET', 'UNDO'}

    filename_ext = ".nif"
    filter_glob: StringProperty(
        default="*.nif",
        options={'HIDDEN'},
    )

    create_bones: bpy.props.BoolProperty(
        name="Create Bones",
        description="Create vanilla bones as needed to make skeleton complete.",
        default=True)

    rename_bones: bpy.props.BoolProperty(
        name="Rename Bones",
        description="Rename bones to conform to Blender's left/right conventions.",
        default=True)

    #rotate_model: bpy.props.BoolProperty(
    #    name="Rotate Model",
    #    description="Rotate model to face forward in blender",
    #    default=True)


    def execute(self, context):
        log.info("NIFLY IMPORT V%d.%d.%d" % bl_info['version'])
        status = {'FINISHED'}

        flags = NifImporter.ImportFlags(0)
        if self.create_bones:
            flags |= NifImporter.ImportFlags.CREATE_BONES
        if self.rename_bones:
            flags |= NifImporter.ImportFlags.RENAME_BONES
        #if self.rotate_model:
        #    flags |= NifImporter.ImportFlags.ROTATE_MODEL

        try:
            NifFile.Load(nifly_path)

            bpy.ops.object.select_all(action='DESELECT')

            NifImporter.do_import(self.filepath, flags)
        
            for area in bpy.context.screen.areas:
                if area.type == 'VIEW_3D':
                    ctx = bpy.context.copy()
                    ctx['area'] = area
                    ctx['region'] = area.regions[-1]
                    bpy.ops.view3d.view_selected(ctx)

        except:
            log.exception("Import of nif failed")
            self.report({"ERROR"}, "Import of nif failed, see console window for details")
            status = {'CANCELLED'}
                
        return status


# ### ---------------------------- TRI Files -------------------------------- ###

def create_shape_keys(obj, tri: TriFile):
    """Adds the shape keys in tri to obj 
        """
    mesh = obj.data
    if mesh.shape_keys is None:
        log.debug(f"Adding first shape key to {obj.name}")
        newsk = obj.shape_key_add()
        mesh.shape_keys.use_relative=True
        newsk.name = "Basis"
        mesh.update()

    base_verts = tri.vertices

    for morph_name, morph_verts in sorted(tri.morphs.items()):
        if morph_name not in mesh.shape_keys.key_blocks:
            newsk = obj.shape_key_add()
            newsk.name = morph_name

            obj.active_shape_key_index = len(mesh.shape_keys.key_blocks) - 1
            #This is a pointer, not a copy
            mesh_key_verts = mesh.shape_keys.key_blocks[obj.active_shape_key_index].data
            #log.debug(f"Morph {morph_name} in tri file should have same number of verts as Blender shape: {len(mesh_key_verts)} != {len(morph_verts)}")
            # We may be applying the morphs to a different shape than the one stored in 
            # the tri file. But the morphs in the tri file are absolute locations, as are 
            # shape key locations. So we need to calculate the offset in the tri and apply that 
            # to our shape keys.
            for key_vert, morph_vert, base_vert in zip(mesh_key_verts, morph_verts, base_verts):
                key_vert.co[0] += morph_vert[0] - base_vert[0]
                key_vert.co[1] += morph_vert[1] - base_vert[1]
                key_vert.co[2] += morph_vert[2] - base_vert[2]
        
            mesh.update()

def create_trip_shape_keys(obj, trip:TripFile):
    """Adds the shape keys in trip to obj 
        """
    mesh = obj.data
    verts = mesh.vertices

    if mesh.shape_keys is None or "Basis" not in mesh.shape_keys.key_blocks:
        newsk = obj.shape_key_add()
        newsk.name = "Basis"

    offsetmorphs = trip.shapes[obj.name]
    for morph_name, morph_verts in sorted(offsetmorphs.items()):
        newsk = obj.shape_key_add()
        newsk.name = ">" + morph_name

        obj.active_shape_key_index = len(mesh.shape_keys.key_blocks) - 1
        #This is a pointer, not a copy
        mesh_key_verts = mesh.shape_keys.key_blocks[obj.active_shape_key_index].data
        #log.debug(f"Morph {morph_name} in tri file should have same number of verts as Blender shape: {len(mesh_key_verts)} != {len(morph_verts)}")
        for vert_index, offsets in morph_verts:
            for i in range(3):
                mesh_key_verts[vert_index].co[i] = verts[vert_index].co[i] + offsets[i]
        
        mesh.update()


def import_trip(filepath, target_objs):
    """ Import a BS Tri file. 
        These TRI files do not have full shape data so they have to be matched to one of the 
        objects in target_objs.
        return = True if the file is a BS Tri file
        """
    result = set()
    trip = TripFile.from_file(filepath)
    if trip.is_valid:
        for shapename, offsetmorphs in trip.shapes.items():
            matchlist = [o for o in target_objs if o.name == shapename]
            if len(matchlist) == 0:
                log.warning(f"BS Tri file shape does not match any selected object: {shapename}")
                result.add('WARNING')
            else:
                create_trip_shape_keys(matchlist[0], trip)
    else:
        result.add('WRONGTYPE')

    return result


def import_tri(filepath, cobj):
    """ Import the tris from filepath into cobj
        If cobj is None, create a new object
        """
    tri = TriFile.from_file(filepath)
    if not type(tri) == TriFile:
        log.error(f"Error reading tri file")
        return None

    new_object = None

    if cobj:
        log.debug(f"Importing with selected object {cobj.name}, type {cobj.type}")
        if cobj.type == "MESH":
            log.debug(f"Selected mesh vertex match: {len(cobj.data.vertices)}/{len(tri.vertices)}")

    # Check whether selected object should receive shape keys
    if cobj and cobj.type == "MESH" and len(cobj.data.vertices) == len(tri.vertices):
        new_object = cobj
        new_mesh = new_object.data
        log.info(f"Verts match, loading tri into existing shape {new_object.name}")
    #elif trip.is_valid:
    #    log.info(f"Loading a Bodyslide TRI -- requires a matching selected mesh")
    #    raise "Cannot import Bodyslide TRI file without a selected object"

    if new_object is None:
        new_mesh = bpy.data.meshes.new(os.path.basename(filepath))
        new_mesh.from_pydata(tri.vertices, [], tri.faces)
        new_object = bpy.data.objects.new(new_mesh.name, new_mesh)

        for f in new_mesh.polygons:
            f.use_smooth = True

        new_mesh.update(calc_edges=True, calc_edges_loose=True)
        new_mesh.validate(verbose=True)

        mesh_create_uv(new_mesh, tri.uv_pos)
   
        new_collection = bpy.data.collections.new(os.path.basename(os.path.basename(filepath) + ".Coll"))
        bpy.context.scene.collection.children.link(new_collection)
        new_collection.objects.link(new_object)
        bpy.context.view_layer.objects.active = new_object
        new_object.select_set(True)

    create_shape_keys(new_object, tri)

    return new_object


def export_tris(nif, trip, obj, verts, tris, uvs, morphdict):
    """ Export a tri file to go along with the given nif file, if there are shape keys 
        and it's not a faceBones nif.
        dict = {shape-key: [verts...], ...} - verts list for each shape which is valid for export.
    """
    result = {'FINISHED'}

    if obj.data.shape_keys is None:
        return result

    fpath = os.path.split(nif.filepath)
    fname = os.path.splitext(fpath[1])

    if fname[0].endswith('_faceBones'):
        return result

    fname_tri = os.path.join(fpath[0], fname[0] + ".tri")
    fname_chargen = os.path.join(fpath[0], fname[0] + "_chargen.tri")

    # Don't export anything that starts with an underscore or asterisk
    objkeys = obj.data.shape_keys.key_blocks.keys()
    export_keys = set(filter((lambda n: n[0] not in ('_', '*') and n != 'Basis'), objkeys))
    expression_morphs = nif.dict.expression_filter(export_keys)
    trip_morphs = set(filter((lambda n: n[0] == '>'), objkeys))
    # Leftovers are chargen candidates
    leftover_morphs = export_keys.difference(expression_morphs).difference(trip_morphs)
    chargen_morphs = nif.dict.chargen_filter(leftover_morphs)

    if len(expression_morphs) > 0 and len(trip_morphs) > 0:
        log.warning(f"Found both expression morphs and BS tri morphs in shape {obj.name}. May be an error.")
        result = {'WARNING'}

    if len(expression_morphs) > 0:
        log.debug(f"....Exporting expressions {expression_morphs}")
        tri = TriFile()
        tri.vertices = verts
        tri.faces = tris
        tri.uv_pos = uvs
        tri.face_uvs = tris # (because 1:1 with verts)
        for m in expression_morphs:
            tri.morphs[m] = morphdict[m]
    
        log.info(f"Generating tri file '{fname_tri}'")
        tri.write(fname_tri, expression_morphs)

    if len(chargen_morphs) > 0:
        log.debug(f"....Exporting chargen morphs {chargen_morphs}")
        tri = TriFile()
        tri.vertices = verts
        tri.faces = tris
        tri.uv_pos = uvs
        tri.face_uvs = tris # (because 1:1 with verts)
        for m in chargen_morphs:
            tri.morphs[m] = morphdict[m]
    
        log.info(f"Generating tri file '{fname_chargen}'")
        tri.write(fname_chargen, chargen_morphs)

    if len(trip_morphs) > 0:
        log.info(f"Generating BS tri shapes for '{obj.name}'")
        trip.set_morphs(obj.name, morphdict, verts)

    return result

class ImportTRI(bpy.types.Operator, ImportHelper):
    """Load a TRI File"""
    bl_idname = "import_scene.niflytri"
    bl_label = "Import TRI (Nifly)"
    bl_options = {'PRESET', 'UNDO'}

    filename_ext = ".tri"
    filter_glob: StringProperty(
        default="*.tri",
        options={'HIDDEN'},
    )

    def execute(self, context):
        log.info("NIFLY IMPORT V%d.%d.%d" % bl_info['version'])
        status = {'FINISHED'}

        try:
            
            v = import_trip(self.filepath, context.selected_objects)
            if 'WRONGTYPE' in v:
                import_tri(self.filepath, bpy.context.object)
            status.union(v)
        
            for area in bpy.context.screen.areas:
                if area.type == 'VIEW_3D':
                    ctx = bpy.context.copy()
                    ctx['area'] = area
                    ctx['region'] = area.regions[-1]
                    bpy.ops.view3d.view_selected(ctx)

            if 'WARNING' in status:
                self.report({"ERROR"}, "Import completed with warnings, see console for details")

        except:
            log.exception("Import of tri failed")
            self.report({"ERROR"}, "Import of tri failed, see console window for details")
            status = {'CANCELLED'}
                
        return status.intersection({'FINISHED', 'CANCELLED'})

# ### ---------------------------- EXPORT -------------------------------- ###

def clean_filename(fn):
    return "".join(c for c in fn.strip() if (c.isalnum() or c in "._- "))

def select_all_faces(mesh):
    """ Make sure all mesh elements are visible and all faces are selected """
    bpy.ops.object.mode_set(mode = 'OBJECT') # Have to be in object mode

    for v in mesh.vertices:
        v.hide = False
    for e in mesh.edges:
        e.hide = False
    for p in mesh.polygons:
        p.hide = False
        p.select = True


def extract_face_info(mesh, uvlayer, use_loop_normals=False):
    """ Extract face info from the mesh. Mesh is triangularized. 
        Return 
        loops = [vert-index, ...] list of vert indices in loops (which are tris)
        uvs = [(u,v), ...] list of uv coordinates 1:1 with loops
        norms = [(x,y,z), ...] list of normal vectors 1:1 with loops
            --Normal vectors come from the loops, because they reflect whether the edges
            are sharp or the object has flat shading
        """
    loops = []
    uvs = []
    norms = []

    # Calculating normals messes up the passed-in UV, so get the data out of it first
    for f in mesh.polygons:
        for i in f.loop_indices:
            uvs.append(uvlayer[i].uv[:])
            #log.debug(f"....Adding uv index {uvlayer[i].uv[:]}")

    # CANNOT figure out how to get the loop normals correctly.  They seem to follow the
    # face normals even on smooth shading.  (TEST_NORMAL_SEAM tests for this.) So use the
    # vertex normal except when there are custom split normals.
    bpy.ops.object.mode_set(mode='OBJECT') #required to get accurate normals
    mesh.calc_normals()
    mesh.calc_normals_split()

    for f in mesh.polygons:
        for i in f.loop_indices:
            loopseg = mesh.loops[i]
            loops.append(loopseg.vertex_index)
            if use_loop_normals:
                norms.append(loopseg.normal[:])
            else:
                norms.append(mesh.vertices[loopseg.vertex_index].normal[:])

    return loops, uvs, norms


def extract_vert_info(obj, mesh, target_key=''):
    """Returns 3 lists of equal length with one entry each for each vertex
        verts = [(x, y, z)... ] - base or as modified by target-key if provided
        weights = [{group-name: weight}... ] - 1:1 with verts list
        dict = {shape-key: [verts...], ...} - verts list for each shape which is valid for export.
            if "target_key" is specified this will be empty
        """
    weights = []
    morphdict = {}

    if target_key != '' and mesh.shape_keys and target_key in mesh.shape_keys.key_blocks.keys():
        log.debug(f"....exporting shape {target_key} only")
        verts = [v.co[:] for v in mesh.shape_keys.key_blocks[target_key].data]
    else:
        verts = [v.co[:] for v in mesh.vertices]

    for v in mesh.vertices:
        vert_weights = {}
        for vg in v.groups:
            try:
                vert_weights[obj.vertex_groups[vg.group].name] = vg.weight
            except:
                log.error(f"ERROR: Vertex #{v.index} references invalid group #{vg.group}")
        weights.append(vert_weights)
    
    if target_key == '' and mesh.shape_keys:
        for sk in mesh.shape_keys.key_blocks:
            morphdict[sk.name] = [v.co[:] for v in sk.data]

    #log.debug(f"....Vertex 18 at {[round(v,2) for v in verts[18]]}")
    return verts, weights, morphdict


def get_bone_xforms(arma, bone_names, shape):
    """Return transforms for the bones in list, getting rotation from what we stashed on import
        arma = data block of armature
        bone_names = list of names
        shape = shape being exported
        result = dict{bone-name: MatTransform, ...}
    """
    result = {}
    for b in arma.bones:
        mat = MatTransform()
        mat.translation = b.head_local
        try:
            mat.rotation = RotationMatrix((tuple(b['pyxform'][0]), 
                                           tuple(b['pyxform'][1]), 
                                           tuple(b['pyxform'][2])))
        except:
            nif = shape.parent
            bone_xform = nif.get_node_xform_to_global(nif.nif_name(b.name)) 
            mat.rotation = bone_xform.rotation
        
        result[b.name] = mat
    
    return result

def export_skin(obj, arma, new_shape, new_xform, weights_by_vert):
    log.info("..Parent is armature, skin the mesh")
    new_shape.skin()
    # if new_shape.has_skin_instance: 
    # just use set_global_to_skin -- it does the check (maybe)
    #if nif.game in ("SKYRIM", "SKYRIMSE"):
    #    new_shape.set_global_to_skindata(new_xform.invert())
    #else:
    #    new_shape.set_global_to_skin(new_xform.invert())
    new_shape.transform = new_xform
    new_shape.set_global_to_skin(new_xform.invert())
    
    group_names = [g.name for g in obj.vertex_groups]
    weights_by_bone = get_weights_by_bone(weights_by_vert)
    used_bones = weights_by_bone.keys()
    arma_bones = get_bone_xforms(arma.data, used_bones, new_shape)
    
    for bone_name, bone_xform in arma_bones.items():
        # print(f"  shape {obj.name} adding bone {bone_name}")
        if bone_name in weights_by_bone and len(weights_by_bone[bone_name]) > 0:
            # print(f"..Shape {obj.name} exporting bone {bone_name} with rotation {bone_xform.rotation.euler_deg()}")
            nifname = new_shape.parent.nif_name(bone_name)
            new_shape.add_bone(nifname, bone_xform)
            log.debug(f"....Adding bone {nifname}")
                #nif.nodes[bone_name].xform_to_global)
            new_shape.setShapeWeights(nifname, weights_by_bone[bone_name])


def tag_unweighted(obj, bones):
    """ Find and return verts that are not weighted to any of the given bones 
        result = (v_index, ...) list of indices into the vertex list
    """
    log.debug(f"..Checking for unweighted verts on {obj.name}")
    unweighted_verts = []
    for v in obj.data.vertices:
        maxweight = 0.0
        if len(v.groups) > 0:
            maxweight = max([g.weight for g in v.groups])
        if maxweight < 0.0001:
            unweighted_verts.append(v.index)
    log.debug(f"..Unweighted vert count: {len(unweighted_verts)}")
    return unweighted_verts


def create_group_from_verts(obj, name, verts):
    """ Create a vertex group from the list of vertex indices.
    Use the existing group if any """
    if name in obj.vertex_groups.keys():
        g = obj.vertex_groups[name]
    else:
        g = obj.vertex_groups.new(name=name)
    g.add(verts, 1.0, 'REPLACE')


def is_facebones(arma):
    #return (fo4FaceDict.matches(set(list(arma.data.bones.keys()))) > 20)
    return  len([x for x in arma.data.bones.keys() if x.startswith('skin_bone_')]) > 5


def best_game_fit(bonelist):
    """ Find the game that best matches the skeleton """
    boneset = set([b.name for b in bonelist])
    maxmatch = 0
    matchgame = ""
    #print(f"Checking bonelist {[b.name for b in bonelist]}")
    for g, s in gameSkeletons.items():
        n = s.matches(boneset)
        #print(f"Checking against game {g} match is {n}")
        if n > maxmatch:
            maxmatch = n
            matchgame = g
    n = fo4FaceDict.matches(boneset)
    if n > maxmatch:
        matchgame = "FO4"
    return matchgame


def expected_game(nif, bonelist):
    """ Check whether the nif's game is the best match for the given bonelist """
    matchgame = best_game_fit(bonelist)
    return matchgame == "" or matchgame == nif.game or \
        (matchgame in ['SKYRIM', 'SKYRIMSE'] and nif.game in ['SKYRIM', 'SKYRIMSE'])


def partitions_from_vert_groups(obj):
    """ Return dictionary of Partition objects for all vertex groups that match the partition 
        name pattern. These are all partition objects including subsegments.
    """
    val = {}
    if obj.vertex_groups:
        for vg in obj.vertex_groups:
            skyid = SkyPartition.name_match(vg.name)
            if skyid >= 0:
                val[vg.name] = SkyPartition(part_id=skyid, flags=0, name=vg.name)
            else:
                segid = FO4Segment.name_match(vg.name)
                if segid >= 0:
                    val[vg.name] = FO4Segment(len(val), 0, name=vg.name)
        
        # A second pass to pick up subsections
        for vg in obj.vertex_groups:
            if vg.name not in val:
                parent_name, subseg_id, material = FO4Subsegment.name_match(vg.name)
                if subseg_id >= 0:
                    if not parent_name in val.keys():
                        # Create parent segments if not there
                        if parent_name == '':
                            parent_name = f"FO4Segment #{len(val)}"
                        parid = FO4Segment.name_match(parent_name)
                        val[parent_name] = FO4Segment(len(val), 0, parent_name)
                    p = val[parent_name]
                    log.debug(f"....Found FO4Subsegment '{vg.name}' child of '{parent_name}'")
                    val[vg.name] = FO4Subsegment(len(val), subseg_id, material, p, name=vg.name)
    
    return val


def all_vertex_groups(weightdict):
    """ Return the set of group names that have non-zero weights """
    val = set()
    for g, w in weightdict.items():
        if w > 0.0001:
            val.add(g)
    return val


def mesh_from_key(editmesh, verts, target_key):
    faces = []
    for p in editmesh.polygons:
        faces.append([editmesh.loops[lpi].vertex_index for lpi in p.loop_indices])
    log.debug(f"....Remaking mesh with shape {target_key}: {len(verts)} verts, {len(faces)} faces")
    newverts = [v.co[:] for v in editmesh.shape_keys.key_blocks[target_key].data]
    newmesh = bpy.data.meshes.new(editmesh.name)
    newmesh.from_pydata(newverts, [], faces)
    return newmesh


def export_shape_to(shape, filepath, game):
    outnif = NifFile()
    outtrip = TripFile()
    outnif.initialize(game, filepath)
    ret = export_shape(outnif, outtrip, shape, '', shape.parent) 
    outnif.save()
    log.info(f"Wrote {filepath}")
    return ret


def get_common_shapes(obj_list):
    """ Return the shape keys found in any of the given objects """
    res = None
    for obj in obj_list:
        o_shapes = set()
        if obj.data.shape_keys:
            o_shapes = set(obj.data.shape_keys.key_blocks.keys())
        if res:
            res = res.union(o_shapes)
        else:
            res = o_shapes
    if res:
        res = list(res)
    return res


def get_with_uscore(str_list):
    return list(filter((lambda x: x[0] == '_'), str_list))


class NifExporter:
    """ Object that handles the export process 
    """
    def __init__(self, filepath, game, rotate=False):
        self.filepath = filepath
        self.game = game
        self.warnings = set()
        self.armature = None
        self.facebones = None
        self.objects = set([])
        self.bg_data = set([])
        self.str_data = set([])
        # Shape keys that start with underscore and are common to all exportable shapes trigger
        # a separate file export for each shape key
        self.file_keys = []
        self.objs_unweighted = set()
        self.objs_scale = set()
        self.objs_mult_part = set()
        self.objs_no_part = set()
        self.arma_game = []
        self.bodytri_written = False
        #self.rotate_model = rotate

    def add_object(self, obj):
        """ Adds the given object to the objects to export """
        if obj.type == 'ARMATURE':
            if self.game == 'FO4' and is_facebones(obj) and self.facebones is None:
                self.facebones = obj
            if self.armature is None:
                self.armature = obj 

        elif obj.type == 'MESH':
            self.objects.add(obj)
            if obj.parent and obj.parent.type == 'ARMATURE':
                self.add_object(obj.parent)
            self.file_keys = get_with_uscore(get_common_shapes(self.objects))

        elif 'BSBehaviorGraphExtraData_Name' in obj.keys():
            self.bg_data.add(obj)

        elif 'NiStringExtraData_Name' in obj.keys():
            self.str_data.add(obj)

        # remove extra data nodes with objects in the export list as parents so they 
        # don't get exported twice
        for n in self.bg_data:
            if n.parent and n.parent in self.objects:
                self.bg_data.remove(n)
        for n in self.str_data:
            if n.parent and n.parent in self.objects:
                self.str_data.remove(n)

    def set_objects(self, objects):
        """ Set the objects to export from the given list of objects 
        """
        for x in objects:
            self.add_object(x)

    def from_context(self, context):
        """ Set the objects to export from the given context 
        """
        self.set_objects(context.selected_objects)


    # --------- DO THE EXPORT ---------

    def export_extra_data(self, nif: NifFile):
        """ Export any extra data represented as Blender objects. 
            Sets self.bodytri_done if one of the extra data nodes represents a bodytri
        """
        exdatalist = [ (x['NiStringExtraData_Name'], x['NiStringExtraData_Value']) for x in \
            self.str_data]
        if len(exdatalist) > 0:
            nif.string_data = exdatalist

        self.bodytri_written = ('BODYTRI' in [x[0] for x in exdatalist])

        bglist = [ (x['BSBehaviorGraphExtraData_Name'], x['BSBehaviorGraphExtraData_Value']) \
                for x in self.bg_data]
        if len(bglist) > 0:
            nif.behavior_graph_data = bglist 


    def export_partitions(self, obj, weights_by_vert, tris):
        """ Export partitions described by vertex groups
            weights = [dict[group-name: weight], ...] vertex weights, 1:1 with verts. For 
                partitions, can assume the weights are 1.0
            tris = [(v1, v2, v3)...] where v1-3 are indices into the vertex list
            returns (partitions, tri_indices)
                partitions = list of partition objects
                tri_indices = list of paritition indices, 1:1 with the shape's tri list
        """
        log.debug(f"..Exporting partitions")
        partitions = partitions_from_vert_groups(obj)
        log.debug(f"....Found partitions {list(partitions.keys())}")

        if len(partitions) == 0:
            return [], []

        partition_set = set(list(partitions.keys()))

        tri_indices = [0] * len(tris)

        for i, t in enumerate(tris):
            # All 3 have to be in the vertex group to count
            vg0 = all_vertex_groups(weights_by_vert[t[0]])
            vg1 = all_vertex_groups(weights_by_vert[t[1]])
            vg2 = all_vertex_groups(weights_by_vert[t[2]])
            tri_partitions = vg0.intersection(vg1).intersection(vg2).intersection(partition_set)
            if len(tri_partitions) > 0:
                #if len(tri_partitions) > 1:
                #    log.warning(f"Found multiple partitions for tri {t} in object {obj.name}: {tri_partitions}")
                #    self.objs_mult_part.add(obj)
                #    create_group_from_verts(obj, MULTIPLE_PARTITION_GROUP, t)

                # Triangulation will put some tris in two partitions. Just choose one--
                # exact division doesn't matter (if it did user should have put in an edge)
                tri_indices[i] = partitions[next(iter(tri_partitions))].id
            else:
                log.warning(f"Tri {t} is not assigned any partition")
                self.objs_no_part.add(obj)
                create_group_from_verts(obj, NO_PARTITION_GROUP, t)

        return list(partitions.values()), tri_indices

    def extract_colors(self, mesh):
        """Extract vertex color data from the given mesh. Use the VERTEX_ALPHA color map
            for alpha values if it exists."""
        vc = mesh.vertex_colors
        alphamap = None
        alphamapname = ''
        colormap = None
        colormapname = ''
        colorlen = 0
        if ALPHA_MAP_NAME in vc.keys():
            alphamap = vc[ALPHA_MAP_NAME].data
            alphamapname = ALPHA_MAP_NAME
            colorlen = len(alphamap)
        if vc.active.data == alphamap:
            # Alpha map is active--see if theres another map to use for colors. If not, 
            # colors will be set to white
            for c in vc:
                if c.data != alphamap:
                    colormap = c.data
                    colormapname = c.name
                    break
        else:
            colormap = vc.active.data
            colormapname = vc.active.name
            colorlen = len(colormap)

        log.debug(f"...Writing vertex colors from map {colormapname}, vertex alpha from {alphamapname}")
        loopcolors = [(0.0, 0.0, 0.0, 0.0)] * colorlen
        for i in range(0, colorlen):
            if colormap:
                c = colormap[i].color[:]
            else:
                c = (1.0, 1.0, 1.0, 1.0)
            if alphamap:
                a = alphamap[i].color
                c = (c[0], c[1], c[2], (a[0] + a[1] + a[2])/3)
            loopcolors[i] = c

        return loopcolors
        #loopcolors = [c.color[:] for c in editmesh.vertex_colors.active.data]

    def extract_mesh_data(self, obj, target_key):
        """ 
        Extract the mesh data from the given object
            obj = object being exported
            target_key = shape key to export
        returns
            verts = list of XYZ vertex locations
            norms_new = list of XYZ normal values, 1:1 with verts
            uvmap_new = list of (u, v) values, 1:1 with verts
            colors_new = list of RGBA color values 1:1 with verts. May be None.
            tris = list of (t1, t2, t3) vert indices to define triangles
            weights_by_vert = [dict[group-name: weight], ...] 1:1 with verts
            morphdict = {shape-key: [verts...], ...} only if "target_key" is NOT specified
        NOTE this routine changes selection and switches to edit mode and back
        """
        originalmesh = obj.data
        editmesh = originalmesh.copy()
        saved_sk = obj.active_shape_key_index
        obj.data = editmesh
        loopcolors = None
        
        original_rot = obj.rotation_euler[:]
        #if self.rotate_model:
        #    obj.rotation_euler = (original_rot[0], original_rot[1], original_rot[2]+pi)

        try:
            bpy.ops.object.select_all(action='DESELECT')
            obj.select_set(True)
            bpy.context.view_layer.objects.active = obj
        
            # If scales aren't uniform, apply them before export
            if (round(obj.scale[0], 4) != round(obj.scale[1], 4)) \
                    or (round(obj.scale[0], 4) != round(obj.scale[2], 4)):
                log.warning(f"Object {obj.name} scale not uniform, applying before export") 
                self.objs_scale.add('SCALE')
                bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        
            # This next little dance ensures the mesh.vertices locations are correct
            obj.active_shape_key_index = 0
            bpy.ops.object.mode_set(mode = 'EDIT')
            bpy.ops.object.mode_set(mode = 'OBJECT')
            #log.debug(f"....Vertex 12 position: {mesh.vertices[12].co}")
        
            # Can't get custom normals out of a bmesh (known limitation). Can't triangulate
            # a regular mesh except through the operator. 
            log.info("..Triangulating mesh")
            select_all_faces(editmesh)
            bpy.ops.object.mode_set(mode = 'EDIT') # Required to convert to tris
            bpy.ops.mesh.quads_convert_to_tris(quad_method='BEAUTY', ngon_method='BEAUTY')
        
            for p in editmesh.polygons:
                p.use_smooth = True
        
            editmesh.update()
         
            verts, weights_by_vert, morphdict = extract_vert_info(obj, editmesh, target_key)
        
            bpy.ops.object.mode_set(mode = 'OBJECT') # Required to get vertex colors
            if len(editmesh.vertex_colors) > 0:
                loopcolors = self.extract_colors(editmesh)
        
            # Apply shape key verts to the mesh so normals will be correct.  If the mesh has
            # custom normals, fukkit -- use the custom normals and assume the deformation
            # won't be so great that it looks bad.
            bpy.ops.object.mode_set(mode = 'OBJECT') 
            uvlayer = editmesh.uv_layers.active.data
            if target_key != '' and \
                editmesh.shape_keys and \
                target_key in editmesh.shape_keys.key_blocks.keys() and \
                not editmesh.has_custom_normals:
                editmesh = mesh_from_key(editmesh, verts, target_key)
        
            loops, uvs, norms = extract_face_info(editmesh, uvlayer, use_loop_normals=editmesh.has_custom_normals)
        
            log.info("..Splitting mesh along UV seams")
            mesh_split_by_uv(verts, norms, loops, uvs, weights_by_vert, morphdict)
            # Old UV map had dups where verts were split; new matches 1-1 with verts
            uvmap_new = [(0.0, 0.0)] * len(verts)
            norms_new = [(0.0, 0.0, 0.0)] * len(verts)
            for i, lp in enumerate(loops):
                assert lp < len(verts), f"Error: Invalid vert index in loops: {lp} >= {len(verts)}"
                uvmap_new[lp] = uvs[i]
                norms_new[lp] = norms[i]
            #uvmap_new = [uvs[loops.index(i)] for i in range(len(verts))]
            #norms_new = [norms[loops.index(i)] for i in range(len(verts))]
        
            # Our "loops" list matches 1:1 with the mesh's loops. So we can use the polygons
            # to pull the loops
            tris = []
            for p in editmesh.polygons:
                tris.append((loops[p.loop_start], loops[p.loop_start+1], loops[p.loop_start+2]))
        
            #tris = [(loops[i], loops[i+1], loops[i+2]) for i in range(0, len(loops), 3)]
            colors_new = None
            if loopcolors:
                log.debug(f"..Exporting vertex colors for shape {obj.name}")
                colors_new = [(0.0, 0.0, 0.0, 0.0)] * len(verts)
                for i, lp in enumerate(loops):
                    colors_new[lp] = loopcolors[i]
            else:
                log.debug(f"..No vertex colors in shape {obj.name}")
        
        finally:
            obj.rotation_euler = original_rot
            obj.data = originalmesh
            obj.active_shape_key_index = saved_sk

        return verts, norms_new, uvmap_new, colors_new, tris, weights_by_vert, morphdict

    def export_shape(self, nif, trip, obj, target_key='', arma=None):
        """Export given blender object to the given NIF file
            nif = target nif file
            trip = target file for BS Tri shapes
            obj = blender object
            target_key = shape key to export
            arma = armature to skin to
            """
        log.info("Exporting " + obj.name)
        log.info(f" . with shapes: {self.file_keys}")

        retval = set()

        is_skinned = (arma is not None)
        unweighted = []
        if UNWEIGHTED_VERTEX_GROUP in obj.vertex_groups:
            obj.vertex_groups.remove(obj.vertex_groups[UNWEIGHTED_VERTEX_GROUP])
        if MULTIPLE_PARTITION_GROUP in obj.vertex_groups:
            obj.vertex_groups.remove(obj.vertex_groups[MULTIPLE_PARTITION_GROUP])
        if NO_PARTITION_GROUP in obj.vertex_groups:
            obj.vertex_groups.remove(obj.vertex_groups[NO_PARTITION_GROUP])
        
        if is_skinned:
            # Get unweighted bones before we muck up the list by splitting edges
            unweighted = tag_unweighted(obj, arma.data.bones.keys())
            if not expected_game(nif, arma.data.bones):
                log.warning(f"Exporting to game that doesn't match armature: game={nif.game}, armature={arma.name}")
                retval.add('GAME')

        verts, norms_new, uvmap_new, colors_new, tris, weights_by_vert, morphdict = \
           self.extract_mesh_data(obj, target_key)

        is_headpart = obj.data.shape_keys \
                and len(nif.dict.expression_filter(set(obj.data.shape_keys.key_blocks.keys()))) > 0
        if is_headpart:
            log.debug(f"...shape is headpart, shape keys = {nif.dict.expression_filter(set(obj.data.shape_keys.key_blocks.keys()))}")

        obj.data.update()
        log.info("..Exporting to nif")
        norms_exp = norms_new
        has_msn = has_msn_shader(obj)
        if has_msn:
            norms_exp = None

        new_shape = nif.createShapeFromData(obj.name, verts, tris, uvmap_new, norms_exp, 
                                            is_headpart, is_skinned)
        if colors_new:
            new_shape.set_colors(colors_new)

        export_shape_data(obj, new_shape)
        
        if obj.active_material:
            export_shader(obj, new_shape)
            log.debug(f"....'{new_shape.name}' has textures: {new_shape.textures}")
            if has_msn:
                new_shape.shader_attributes.shaderflags1_set(ShaderFlags1.MODEL_SPACE_NORMALS)
            else:
                new_shape.shader_attributes.shaderflags1_clear(ShaderFlags1.MODEL_SPACE_NORMALS)
            if colors_new:
                new_shape.shader_attributes.shaderflags2_set(ShaderFlags2.VERTEX_COLORS)
            else:
                new_shape.shader_attributes.shaderflags2_clear(ShaderFlags2.VERTEX_COLORS)
            new_shape.save_shader_attributes()
        else:
            log.debug(f"..No material on {obj.name}")

        if is_skinned:
            nif.createSkin()

        new_xform = MatTransform();
        new_xform.translation = obj.location
        #new_xform.rotation = RotationMatrix((obj.matrix_local[0][0:3], 
        #                                     obj.matrix_local[1][0:3], 
        #                                     obj.matrix_local[2][0:3]))
        new_xform.rotation = RotationMatrix.from_euler_rad(*obj.rotation_euler[:])
        new_xform.scale = obj.scale[0]
        
        if is_skinned:
            export_skin(obj, arma, new_shape, new_xform, weights_by_vert)
            if len(unweighted) > 0:
                create_group_from_verts(obj, UNWEIGHTED_VERTEX_GROUP, unweighted)
                log.warning("Some vertices are not weighted to the armature in object {obj.name}")
                self.objs_unweighted.add(obj)

            partitions, tri_indices = self.export_partitions(obj, weights_by_vert, tris)
            if len(partitions) > 0:
                if 'FO4_SEGMENT_FILE' in obj.keys():
                    log.debug(f"....Writing segment file {obj['FO4_SEGMENT_FILE']}")
                    new_shape.segment_file = obj['FO4_SEGMENT_FILE']
                new_shape.set_partitions(partitions, tri_indices)
        else:
            log.debug(f"...Exporting {new_shape.name} with transform {new_xform}")
            new_shape.transform = new_xform

        retval |= export_tris(nif, trip, obj, verts, tris, uvmap_new, morphdict)

        log.info(f"..{obj.name} successfully exported to {nif.filepath}")
        return retval

    def export_file_set(self, arma, suffix=''):
        """ Create a set of nif files from the given object, using the given armature and appending
            the suffix. One file is created per shape key with the shape key used as suffix. Associated
            TRIP files are exported if there is TRIP info.
                arma = skeleton to use 
                suffix = suffix to append to the filenames, after the shape key suffix
            """
        if self.file_keys is None or len(self.file_keys) == 0:
            shape_keys = ['']
        else:
            shape_keys = self.file_keys

        for sk in shape_keys:
            fname_ext = os.path.splitext(os.path.basename(self.filepath))
            fbasename = fname_ext[0] + sk + suffix
            fnamefull = fbasename + fname_ext[1]
            fpath = os.path.join(os.path.dirname(self.filepath), fnamefull)

            log.info(f"..Exporting to {self.game} {fpath}")
            exportf = NifFile()
            exportf.initialize(self.game, fpath)
            if suffix == '_faceBones':
                exportf.dict = fo4FaceDict

            self.export_extra_data(exportf)

            trip = TripFile()
            trippath = os.path.join(os.path.dirname(self.filepath), fbasename) + ".tri"

            for obj in self.objects:
                self.export_shape(exportf, trip, obj, sk, arma)
                log.debug(f"Exported shape {obj.name}")

            # Check for bodytri morphs--write the extra data node if needed
            if len(trip.shapes) > 0 and not self.bodytri_written:
                exportf.string_data = [('BODYTRI', truncate_filename(trippath, "meshes"))]

            exportf.save()
            log.info(f"..Wrote {fpath}")

            if len(trip.shapes) > 0:
                trip.write(trippath)
                log.info(f"..Wrote {trippath}")


    def execute(self):
        log.debug(f"..Exporting objects: {self.objects}\nstring data: {self.str_data}\nBG data: {self.bg_data}\narmature: armatrue: {self.armature},\nfacebones: {self.facebones}")
        NifFile.clear_log()
        if self.facebones:
            self.export_file_set(self.facebones, '_faceBones')
        if self.armature:
            self.export_file_set(self.armature, '')
        if self.facebones is None and self.armature is None:
            self.export_file_set(None, '')
    
    def export(self, objects):
        self.set_objects(objects)
        self.execute()

    @classmethod
    def do_export(cls, filepath, game, objects):
        return NifExporter(filepath, game).export(objects)
        

class ExportNIF(bpy.types.Operator, ExportHelper):
    """Export Blender object(s) to a NIF File"""

    bl_idname = "export_scene.nifly"
    bl_label = 'Export NIF (Nifly)'
    bl_options = {'PRESET'}

    filename_ext = ".nif"
    
    target_game: EnumProperty(
            name="Target Game",
            items=(('SKYRIM', "Skyrim", ""),
                   ('SKYRIMSE', "Skyrim SE", ""),
                   ('FO4', "Fallout 4", ""),
                   ('FO76', "Fallout 76", ""),
                   ('FO3', "Fallout New Vegas", ""),
                   ('FO3', "Fallout 3", ""),
                   ),
            )

    #rotate_model: bpy.props.BoolProperty(
    #    name="Rotate Model",
    #    description="Rotate model from blender-forward to nif-forward",
    #    default=True)


    def __init__(self):
        obj = bpy.context.object
        if obj is None:
            self.report({"ERROR"}, "No active object to export")
            return

        self.filepath = clean_filename(obj.name)
        arma = None
        if obj.type == "ARMATURE":
            arma = obj
        else:
            if obj.parent and obj.parent.type == "ARMATURE":
                arma = obj.parent
        if arma:
            g = best_game_fit(arma.data.bones)
            if g != "":
                self.target_game = g
        
    @classmethod
    def poll(cls, context):
        if context.object is None:
            log.error("Must select an object to export")
            return False

        if context.object.mode != 'OBJECT':
            log.error("Must be in Object Mode to export")
            return False

        return True

    def execute(self, context):
        res = set()

        if not self.poll(context):
            self.report({"ERROR"}, f"Cannot run exporter--see system console for details")
            return {'CANCELLED'} 

        log.info("NIFLY EXPORT V%d.%d.%d" % bl_info['version'])
        NifFile.Load(nifly_path)

        try:
            exporter = NifExporter(self.filepath, self.target_game) # , rotate=self.rotate_model)
            exporter.from_context(context)
            exporter.export(context.selected_objects)
            
            rep = False
            if len(exporter.objs_unweighted) > 0:
                self.report({"ERROR"}, f"The following objects have unweighted vertices.See the '*UNWEIGHTED*' vertex group to find them: \n{exporter.objs_unweighted}")
                rep = True
            if len(exporter.objs_scale) > 0:
                self.report({"ERROR"}, f"The following objects have non-uniform scale, which nifs do not support. Scale applied to verts before export.\n{exporter.objs_scale}")
                rep = True
            if len(exporter.objs_mult_part) > 0:
                self.report({'WARNING'}, f"Some faces have been assigned to more than one partition, which should never happen.\n{exporter.objs_mult_part}")
                rep = True
            if len(exporter.objs_no_part) > 0:
                self.report({'WARNING'}, f"Some faces have been assigned to no partition, which should not happen for skinned body parts.\n{exporter.objs_no_part}")
                rep = True
            if len(exporter.arma_game) > 0:
                self.report({'WARNING'}, f"The armature appears to be designed for a different game--check that it's correct\nArmature: {exporter.arma_game}, game: {exportf.game}")
                rep = True
            if 'NOTHING' in exporter.warnings:
                self.report({'WARNING'}, f"No mesh selected; nothing to export")
                rep = True
            if 'WARNING' in exporter.warnings:
                self.report({'WARNING'}, f"Export completed with warnings. Check the console window.")
                rep = True
            if not rep:
                self.report({'INFO'}, f"Export successful")
            
        except:
            log.exception("Export of nif failed")
            self.report({"ERROR"}, "Export of nif failed, see console window for details")
            res.add("CANCELLED")

        return res.intersection({'CANCELLED'}, {'FINISHED'})


def nifly_menu_import_nif(self, context):
    self.layout.operator(ImportNIF.bl_idname, text="Nif file with Nifly (.nif)")
def nifly_menu_import_tri(self, context):
    self.layout.operator(ImportTRI.bl_idname, text="Tri file with Nifly (.tri)")
def nifly_menu_export(self, context):
    self.layout.operator(ExportNIF.bl_idname, text="Nif file with Nifly (.nif)")

def register():
    bpy.utils.register_class(ImportNIF)
    bpy.utils.register_class(ImportTRI)
    bpy.utils.register_class(ExportNIF)
    bpy.types.TOPBAR_MT_file_import.append(nifly_menu_import_nif)
    bpy.types.TOPBAR_MT_file_import.append(nifly_menu_import_tri)
    bpy.types.TOPBAR_MT_file_export.append(nifly_menu_export)

def unregister():
    bpy.types.TOPBAR_MT_file_import.remove(nifly_menu_import_nif)
    bpy.types.TOPBAR_MT_file_import.remove(nifly_menu_import_tri)
    bpy.types.TOPBAR_MT_file_export.remove(nifly_menu_export)
    bpy.utils.unregister_class(ImportNIF)
    bpy.utils.unregister_class(ExportNIF)



def run_tests():
    print("""
    ############################################################
    ##                                                        ##
    ##                        TESTING                         ##
    ##                                                        ##
    ############################################################
    """)

    from test_tools import test_title, clear_all, append_from_file, export_from_blend, find_vertex, remove_file
    from pynifly_tests import run_tests

    TEST_MUTANT = False
    TEST_RENAME = False
    TEST_BONE_XPORT_POS = False
    TEST_EXPORT_HANDS = False
    TEST_POT = False
   # TEST_ROT = False
    TEST_SCALING = False
    TEST_UNIT = True

    NifFile.Load(nifly_path)
    #LoggerInit()

    clear_all()

    if TEST_BPY_ALL:
        run_tests(pynifly_dev_path, NifExporter, NifImporter, import_tri)

    # TESTS
    # These are tests of functionality currently under development




# #############################################################################################
#
#    REGRESSION TESTS
#
#    These tests cover specific cases that have caused bugs in the past.
#
# ############################################################################################

    if TEST_BPY_ALL or TEST_TIGER_EXPORT:
        print("### TEST_TIGER_EXPORT: Tiger head exports without errors")

        clear_all()
        remove_file(os.path.join(pynifly_dev_path, r"tests/Out/TEST_TIGER_EXPORT.nif"))
        remove_file(os.path.join(pynifly_dev_path, r"tests/Out/TEST_TIGER_EXPORT_faceBones.nif"))
        remove_file(os.path.join(pynifly_dev_path, r"tests/Out/TEST_TIGER_EXPORT.tri"))
        remove_file(os.path.join(pynifly_dev_path, r"tests/Out/TEST_TIGER_EXPORT_chargen.tri"))

        append_from_file("TigerMaleHead", True, r"tests\FO4\Tiger.blend", r"\Object", "TigerMaleHead")

        exporter = NifExporter(os.path.join(pynifly_dev_path, r"tests/Out/TEST_TIGER_EXPORT.nif"), 
                               'FO4')
        exporter.export([bpy.data.objects["TigerMaleHead"]])

        nif1 = NifFile(os.path.join(pynifly_dev_path, r"tests/Out/TEST_TIGER_EXPORT.nif"))
        assert len(nif1.shapes) == 1, f"Expected tiger nif"


    if TEST_BPY_ALL or TEST_3BBB:
        print("## TEST_3BBB: Test that this mesh imports with the right transforms")
        
        clear_all()
        testfile = os.path.join(pynifly_dev_path, r"tests/SkyrimSE/3BBB_femalebody_1.nif")
        NifImporter.do_import(testfile)
        
        obj = bpy.context.object
        assert obj.location[0] == 0, f"Expected body to be centered on x-axis, got {obj.location[:]}"

        print("## Test that the same armature is used for the next import")
        arma = bpy.data.objects['Scene Root']
        bpy.ops.object.select_all(action='DESELECT')
        arma.select_set(True)
        bpy.context.view_layer.objects.active = arma
        testfile2 = os.path.join(pynifly_dev_path, r"tests/SkyrimSE/3BBB_femalehands_1.nif")
        NifImporter.do_import(testfile2)

        arma2 = bpy.context.object.parent
        assert arma2.name == arma.name, f"Should have parented to same armature: {arma2.name} != {arma.name}"

    if TEST_BPY_ALL or TEST_MUTANT:
        print("### TEST_MUTANT: Test that the supermutant body imports correctly the *second* time")

        clear_all()
        testfile = os.path.join(pynifly_dev_path, r"tests/FO4/testsupermutantbody.nif")
        imp = NifImporter.do_import(testfile, NifImporter.ImportFlags.RENAME_BONES)
        log.debug(f"Expected -140 z translation in first nif, got {imp.nif.shapes[0].global_to_skin.translation[2]}")

        sm1 = bpy.context.object
        assert round(sm1.location[2]) == 140, f"Expect first supermutant body at 140 Z, got {sm1.location[2]}"
        assert round(imp.nif.shapes[0].global_to_skin.translation[2]) == -140, f"Expected -140 z translation in first nif, got {imp.nif.shapes[0].global_to_skin.translation[2]}"

        imp2 = NifImporter.do_import(testfile, NifImporter.ImportFlags.RENAME_BONES)
        sm2 = bpy.context.object
        assert round(sm2.location[2]) == 140, f"Expect supermutant body at 140 Z, got {sm2.location[2]}"

        
    if TEST_BPY_ALL or TEST_RENAME:
        print("### TEST_RENAME: Test that renaming bones works correctly")

        clear_all()
        testfile = os.path.join(pynifly_dev_path, r"C:\Users\User\OneDrive\Dev\PyNifly\PyNifly\tests\Skyrim\femalebody_1.nif")
        imp = NifImporter.do_import(testfile, NifImporter.ImportFlags.CREATE_BONES)

        body = bpy.context.object
        vgnames = [x.name for x in body.vertex_groups]
        vgxl = list(filter(lambda x: ".L" in x or ".R" in x, vgnames))
        assert len(vgxl) == 0, f"Expected no vertex groups renamed, got {vgxl}"

        armnames = [b.name for b in body.parent.data.bones]
        armxl = list(filter(lambda x: ".L" in x or ".R" in x, armnames))
        assert len(armxl) == 0, f"Expected no bones renamed in armature, got {vgxl}"


    if TEST_BPY_ALL or TEST_BONE_XPORT_POS:
        print("### Test that bones named like vanilla bones but from a different skeleton export to the correct position")

        clear_all()
        testfile = os.path.join(pynifly_dev_path, r"tests\Skyrim\draugr.nif")
        imp = NifImporter.do_import(testfile, 0)
        draugr = bpy.context.object
        spine2 = draugr.parent.data.bones['NPC Spine2 [Spn2]']
        assert round(spine2.head[2], 2) == 102.36, f"Expected location at z 102.36, found {spine2.head[2]}"

        outfile = os.path.join(pynifly_dev_path, r"tests/Out/TEST_BONE_XPORT_POS.nif")
        exp = NifExporter(outfile, 'SKYRIM')
        exp.export([bpy.data.objects["Body_Male_Naked"]])

        impcheck = NifImporter.do_import(outfile, 0)

        nifbone = impcheck.nif.nodes['NPC Spine2 [Spn2]']
        assert round(nifbone.transform.translation[2], 2) == 102.36, f"Expected nif location at z 102.36, found {nifbone.transform.translation[2]}"

        draugrcheck = bpy.context.object
        spine2check = draugrcheck.parent.data.bones['NPC Spine2 [Spn2]']
        assert round(spine2check.head[2], 2) == 102.36, f"Expected location at z 102.36, found {spine2check.head[2]}"


    if TEST_BPY_ALL or TEST_EXPORT_HANDS:
        print("### Test that hand mesh doesn't throw an error")

        outfile = os.path.join(pynifly_dev_path, r"tests/Out/TEST_EXPORT_HANDS.tri")
        remove_file(outfile)

        append_from_file("SupermutantHands", True, r"tests\FO4\SupermutantHands.blend", r"\Object", "SupermutantHands")
        bpy.ops.object.select_all(action='SELECT')

        exp = NifExporter(outfile, 'FO4')
        exp.export(bpy.context.selected_objects)

        assert os.path.exists(outfile)


    if TEST_BPY_ALL or TEST_SCALING:
        print("### Test that scale factors happen correctly")

        clear_all()
        testfile = os.path.join(pynifly_dev_path, r"tests\Skyrim\statuechampion.nif")
        NifImporter.do_import(testfile, 0)
        
        base = bpy.data.objects['basis1']
        assert int(base.scale[0]) == 10, f"ERROR: Base scale should be 10, found {base.scale[0]}"
        tail = bpy.data.objects['tail_base.001']
        assert round(tail.scale[0], 1) == 1.7, f"ERROR: Tail scale should be ~1.7, found {tail.scale}"
        assert round(tail.location[0], 0) == -158, f"ERROR: Tail x loc should be -158, found {tail.location}"

        testout = os.path.join(pynifly_dev_path, r"tests\Out\TEST_SCALING.nif")
        exp = NifExporter.do_export(testout, "SKYRIM", bpy.data.objects[:])
        checknif = NifFile(testout)
        checkfoot = checknif.shape_dict['FootLowRes']
        assert checkfoot.transform.rotation.matrix[0][0] == 1.0, f"ERROR: Foot rotation matrix not identity: {checkfoot.transform.rotation.matrix}"
        assert checkfoot.transform.scale == 1.0, f"ERROR: Foot scale not correct: {checkfoot.transform.scale}"
        checkbase = checknif.shape_dict['basis3']
        assert checkbase.transform.rotation.matrix[0][0] == 1.0, f"ERROR: Base rotation matrix not identity: {checkbase.transform.rotation.matrix}"
        assert checkbase.transform.scale == 10.0, f"ERROR: Base scale not correct: {checkbase.transform.scale}"


    if TEST_BPY_ALL or TEST_POT:
        print("### Test that pot shaders doesn't throw an error")

        clear_all()
        testfile = os.path.join(pynifly_dev_path, r"tests\SkyrimSE\spitpotopen01.nif")
        imp = NifImporter.do_import(testfile, 0)
        assert 'ANCHOR:0' in bpy.data.objects.keys()


    #if TEST_BPY_ALL or TEST_ROT:
    #    print("### Test that rotating the model works correctly")

    #    clear_all()
    #    testfile = os.path.join(pynifly_dev_path, r"tests\Skyrim\malehead.nif")
    #    imp = NifImporter.do_import(testfile, 
    #                                NifImporter.ImportFlags.CREATE_BONES | 
    #                                NifImporter.ImportFlags.RENAME_BONES |
    #                                NifImporter.ImportFlags.ROTATE_MODEL )
    #    assert 'MaleHeadIMF' in bpy.data.objects.keys()
    #    head = bpy.data.objects['MaleHeadIMF']
    #    assert round(head.rotation_euler[2], 4) == round(math.pi, 4), f"Error: Head should have been rotated, found {head.rotation_euler[:]}"
    #    assert 'MaleHead.nif' in bpy.data.objects.keys()
    #    skel = bpy.data.objects['MaleHead.nif']
    #    assert skel.rotation_euler[:] == (0, 0, math.pi), f"Error: Armature should have been rotated, found {skel.rotation_euler[:]}"


    if TEST_UNIT:
        # Lower-level tests of individual routines for bug hunting
        test_title("TEST_UNIT", "get_weights_by_bone converts from weights-by-vertex")

        group_names = ("a", "b", "c", "d")
        wbv = [{"a": 0.1, "c": 0.5}, {"b": 0.2}, {"d": 0.0, "b": 0.6}, {"a": 0.4}]
        wbb = get_weights_by_bone(wbv)
        assert wbb["a"] == [(0, 0.1), (3, 0.4)], "ERROR: get_weights_by_bone failed"
        assert wbb["b"] == [(1, 0.2), (2, 0.6)], "ERROR: get_weights_by_bone failed"
        assert wbb["c"] == [(0, 0.5)], "ERROR: get_weights_by_bone failed"


    print("""
    ############################################################
    ##                                                        ##
    ##                    TESTS DONE                          ##
    ##                                                        ##
    ############################################################
    """)


if __name__ == "__main__":
    try:
        do_run_tests = False
        if RUN_TESTS == True:
            do_run_tests = True
    except:
        do_run_tests == False
        
    if not do_run_tests:
        try:
            unregister()
        except:
            pass
        register()
    else:
        try:
            run_tests()
        except:
            traceback.print_exc()
