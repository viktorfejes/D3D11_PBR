import bpy

# Select the mesh object
obj = bpy.context.object
mesh = obj.data

# Ensure you're in Object mode to modify selections
bpy.ops.object.mode_set(mode='OBJECT')

# IDs from RizomUV
vertex_ids = [336864, 336782]

# Safety: Check bounds
max_index = len(mesh.vertices) - 1
for i in vertex_ids:
    if i > max_index:
        print(f"⚠ Vertex ID {i} is out of range (max {max_index})")
        continue
    mesh.vertices[i].select = True
    print(f"✅ Selected vertex {i} at {mesh.vertices[i].co}")

# Switch to Edit mode to inspect in viewport
bpy.ops.object.mode_set(mode='EDIT')
