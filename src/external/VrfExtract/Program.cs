using System.Globalization;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using SteamDatabase.ValvePak;
using ValveKeyValue;
using ValveResourceFormat;
using ValveResourceFormat.ResourceTypes;
using ValveResourceFormat.ResourceTypes.RubikonPhysics;
using ValveResourceFormat.Serialization.KeyValues;
using static ValveResourceFormat.ResourceTypes.RubikonPhysics.Shapes.Hull;
using static ValveResourceFormat.ResourceTypes.RubikonPhysics.Shapes.Mesh;

if (args.Length < 2) {
    Console.Error.WriteLine("Usage: VrfExtract <world_physics.vmdl_c> <output.tri> [cs2_root]");
    return 1;
}

using var stream = File.OpenRead(args[0]);
var resource = new Resource();
resource.Read(stream);
if (FindPhys(resource) is not { } phys) {
    Console.Error.WriteLine("No PHYS block in resource");
    return 1;
}

var (hashToName, nameToMod) = args.Length > 2 ? LoadSurfaces(args[2]) : ([], new(StringComparer.OrdinalIgnoreCase));
var count = Math.Max(phys.CollisionAttributes.Count, phys.SurfacePropertyHashes.Length);
var penCosts = new float[count];
for (var i = 0; i < count; i++) {
    var name = hashToName.GetValueOrDefault(i < phys.SurfacePropertyHashes.Length ? phys.SurfacePropertyHashes[i] : 0u, "default");
    penCosts[i] = PenCost(name, nameToMod.GetValueOrDefault(name, 1f));
}

var def = DefaultCollision(phys.CollisionAttributes);
var tris = new List<TriangleCombined>();
for (var p = 0; p < phys.Parts.Length; p++) {
    var shape = phys.Parts[p].Shape;
    var pose = phys.BindPose.Length == 0 ? Matrix4x4.Identity : phys.BindPose[p];
    foreach (var hull in shape.Hulls) {
        if (!def.Contains(hull.CollisionAttributeIndex)) continue;
        var verts = Transform(hull.Shape.GetVertexPositions(), pose);
        TriangulateHull(hull.Shape.GetFaces(), hull.Shape.GetEdges(), verts, (ushort)hull.CollisionAttributeIndex, tris);
    }
    foreach (var mesh in shape.Meshes) {
        if (!def.Contains(mesh.CollisionAttributeIndex)) continue;
        var verts = Transform(mesh.Shape.GetVertices(), pose);
        foreach (var tri in mesh.Shape.GetTriangles())
            tris.Add(new TriangleCombined(verts[tri.X], verts[tri.Y], verts[tri.Z], (ushort)mesh.CollisionAttributeIndex));
    }
}

WriteTri2(args[1], tris, penCosts);
Console.Error.WriteLine($"Wrote {tris.Count} triangles");
return 0;

static PhysAggregateData? FindPhys(Resource resource) {
    if (resource.DataBlock is PhysAggregateData p)
        return p;
    foreach (var block in resource.Blocks)
        if (block is PhysAggregateData phys)
            return phys;
    return null;
}

static HashSet<int> DefaultCollision(IReadOnlyList<KVObject> attrs) {
    var set = new HashSet<int>();
    for (var i = 0; i < attrs.Count; i++) {
        var g = attrs[i].GetStringProperty("m_CollisionGroupString");
        if (string.IsNullOrEmpty(g) || g.Equals("default", StringComparison.OrdinalIgnoreCase))
            set.Add(i);
    }
    if (set.Count == 0 && attrs.Count > 0) set.Add(0);
    return set;
}

static (Dictionary<uint, string>, Dictionary<string, float>) LoadSurfaces(string root) {
    var names = new Dictionary<uint, string>();
    var mods = new Dictionary<string, float>(StringComparer.OrdinalIgnoreCase);
    var vpk = Path.Combine(root, "game", "csgo", "pak01_dir.vpk");
    if (!File.Exists(vpk)) return (names, mods);

    var pak = new Package();
    pak.Read(vpk);
    if (VpkRead(pak, "surfaceproperties/surfaceproperties.vsurf_c") is { } vsurf) {
        var r = new Resource();
        using var ms = new MemoryStream(vsurf);
        r.Read(ms);
        ParseSurfaces(r.DataBlock?.ToString() ?? "", names, null);
    }
    if (VpkRead(pak, "scripts/surfaceproperties_game.txt") is { } game)
        ParseSurfaces(Encoding.UTF8.GetString(game), null, mods);
    return (names, mods);
}

static byte[]? VpkRead(Package pak, string entry) {
    foreach (var list in pak.Entries!.Values)
        foreach (var e in list)
            if (e.GetFullPath().Equals(entry, StringComparison.OrdinalIgnoreCase)) {
                pak.ReadEntry(e, out var data);
                return data.Length > 0 ? data : null;
            }
    return null;
}

static void ParseSurfaces(string text, Dictionary<uint, string>? names, Dictionary<string, float>? mods) {
    foreach (Match m in Regex.Matches(text, @"surfacePropertyName\s*=\s*""([^""]+)""")) {
        var name = m.Groups[1].Value;
        var chunk = text.Substring(m.Index, Math.Min(800, text.Length - m.Index));
        if (names != null && Regex.Match(chunk, @"m_nameHash\s*=\s*(0x[\da-fA-F]+|\d+)") is { Success: true } hm)
            names[ParseUInt(hm.Groups[1].Value)] = name;
        if (mods != null && Regex.Match(chunk, @"bulletPenetrationDistanceModifier\s*=\s*([\d.]+)") is { Success: true } mm)
            mods[name] = float.Parse(mm.Groups[1].Value, CultureInfo.InvariantCulture);
    }
}

static uint ParseUInt(string s) => s.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
    ? uint.Parse(s[2..], NumberStyles.HexNumber)
    : uint.Parse(s);

static float PenCost(string name, float mod) {
    var n = name.ToLowerInvariant();
    if (n.Contains("concrete") || n.Contains("brick") || n.Contains("rock")) return 999f;
    return Math.Min(90f / Math.Max(mod, 0.1f), 250f);
}

static Vector3[] Transform(ReadOnlySpan<Vector3> src, Matrix4x4 pose) {
    var dst = new Vector3[src.Length];
    for (var i = 0; i < src.Length; i++) dst[i] = Vector3.Transform(src[i], pose);
    return dst;
}

static void TriangulateHull(ReadOnlySpan<Face> faces, ReadOnlySpan<HalfEdge> edges, Vector3[] verts, ushort attr, List<TriangleCombined> outTris) {
    foreach (var face in faces) {
        var start = face.Edge;
        for (var edge = edges[start].Next; edge != start;) {
            var next = edges[edge].Next;
            if (next == start) break;
            outTris.Add(new TriangleCombined(verts[edges[start].Origin], verts[edges[edge].Origin], verts[edges[next].Origin], attr));
            edge = next;
        }
    }
}

static void WriteTri2(string path, List<TriangleCombined> tris, float[] penCosts) {
    Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
    using var bw = new BinaryWriter(File.Create(path));
    bw.Write(Encoding.ASCII.GetBytes("TRI2"));
    bw.Write((uint)tris.Count);
    bw.Write((uint)penCosts.Length);
    foreach (var c in penCosts) bw.Write(c);
    foreach (var t in tris) {
        bw.Write(t.V0.X); bw.Write(t.V0.Y); bw.Write(t.V0.Z);
        bw.Write(t.V1.X); bw.Write(t.V1.Y); bw.Write(t.V1.Z);
        bw.Write(t.V2.X); bw.Write(t.V2.Y); bw.Write(t.V2.Z);
        bw.Write(t.Attr);
        bw.Write((ushort)0);
    }
}

[StructLayout(LayoutKind.Sequential)]
readonly struct TriangleCombined(Vector3 v0, Vector3 v1, Vector3 v2, ushort attr) {
    public readonly Vector3 V0 = v0, V1 = v1, V2 = v2;
    public readonly ushort Attr = attr;
}
