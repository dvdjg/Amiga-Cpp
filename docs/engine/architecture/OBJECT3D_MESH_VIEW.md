# object3d: vista tipada del mesh `obj2c` (diseño)

Rediseño del acceso a la malla `obj2c` de `eng::object3d` para que sea **C++ tipado y seguro**, no
una "conversión cutre de C". Sustituye el modelo `Mesh3D`/`Object3D` con `Span<u8>` + punteros
crudos + wrappers libres. Ver `CODING_STYLE.md` §"Seguridad de tipos sobre punteros crudos".

## 1. El problema (qué huele a C)

- **Wrappers de una línea** (`node3d(b,i){return b.node(i);}`): ceremonia sin valor.
- **`Object3D { u8* objdat; }`** público: el tipo no dice nada y cualquiera toca bytes.
- **Grupos como `s16* group; while ((i = *group++))`**: puntero crudo + centinela a mano.
- **Validación dispersa** (`mesh_validate` con `const u8* base`, casts sueltos).
- **Un `s16` es offset, índice y dato a la vez**: sin tipos fuertes que los distingan.

El formato `obj2c` **no es el culpable**: es un layout empaquetado válido (variable, indexado por
offset de byte) y lo lee `flatshade_asm.s`. Lo que falta es **tipar el acceso**, no cambiar el
formato.

## 2. Diseño propuesto

```
  ┌───────────────────────── MeshBlob (frontera byte->struct, validada) ─────────────────────────┐
  │  Span<u8> m_blob                                                                            │
  │   node(VertexRef)->Ref<Node3D>   point/vertex(VertexRef)   edge(EdgeRef)   face(FaceRef)      │
  │   indices(FaceRef)->Span<FaceIndex>            points()/edges()/faces() -> rangos tipados    │
  └───────────────────────────────────────────────▲─────────────────────────────────────────────┘
                                                   │ compone
  ┌────────────────── Object3D ───────────────────────────────────────────────────────────────┐
  │  MeshAbi m_abi;   // objdat@0, vertexGroups@4, edgeGroups@8, faceGroups@12, objects@16 (asm) │
  │  Angle3 rotate; Point3R scale; Point3C translate; Affine3<> objectToWorld/worldToObject;     │
  │  Point3C camera;                                                                             │
  └──────────────────────────────────────────────────────────────────────────────────────────┘
```

**Piezas:**

- **Tipos de pieza** (layout intacto; ya en `Fixed`): `Node3D`/`Edge`/`Face`/`FaceIndex`.
- **Refs fuertes**: `ObjOffset{ s16 bytes; }` y `VertexRef`/`EdgeRef`/`FaceRef` (cada uno envuelve un
  `ObjOffset`). El compilador impide mezclarlos y un offset deja de ser "un `s16` cualquiera".
- **`MeshBlob`**: **única** frontera byte→struct (`Span<u8>` + `check()`). `check(vertexGroups, edgeGroups, faceGroups)` → `Status` valida el blob y los grupos (offsets en rango y alineados, caras con `count >= 0` y `FaceIndex` dentro del blob) sin recorrer memoria fuera del blob. Acceso **comprobado** devolviendo `Ref<T>` (nunca `T*`). Los **grupos viven en el descriptor** (`Mesh3D`); el `Object3D` los copia a su bloque de ABI. Grupos como **rangos** (saltan el centinela 0): `for (VertexRef v : obj.points())`.
- **`Object3D` compone** la vista y guarda el estado tipado. La **ABI del asm** (los offsets de
  `flatshade_asm.s`) queda **aislada y documentada** en `MeshAbi` (5 punteros @0/4/8/12/16) — no es la
  superficie pública.
- **`new_object3d_checked()`** devuelve `MeshBlob::Status` (no `void` con `__builtin_trap`).
- **Se borran los wrappers libres**: la API son los métodos de la vista/objeto.

## 3. Alternativas para el layout `obj2c`

1. **Blob empaquetado + vista tipada (esta propuesta).** Cero RAM extra, compatible con el asm (vía
   `MeshAbi`) y con el `.c` embebido. Es la mejor donde el heap no existe y el mesh va en ROM.
2. **Mesh "cocinado"** (`CookedMesh { Span<Node3D> nodes; Span<Edge> edges; Span<Face> faces;
   Span<FaceIndex> indices; }`): parseo **una vez** al cargar a arrays contiguos, y el asm lee esos
   arrays. Consumo más simple, pero exige un buffer de cocinado (`Block<Fast>`) y adaptar el asm.
   Útil si el mesh se recarga mucho o si se quiere exponer `Span` contiguos al juego.

**Recomendada la 1**: mantiene la fuente de verdad, no añade RAM y pone el tipo en el acceso.

## 4. Migración (por fases, sin big-bang)

- **Fase A**: `ObjOffset`/refs + `MeshBlob` (`check`, acceso `Ref`, rangos). Migrar los
  accesores a la vista y **borrar los wrappers libres**.
- **Fase B**: grupos como rangos; migrar `lib3d` + demos (117/116/118/079).
- **Fase C**: `Object3D` compone `MeshBlob` + `MeshAbi` (privado); `new_object3d_checked` devuelve `Status`.
- **Fase D**: `mesh_validate` se disuelve en `MeshBlob::check`; HOST-014/047/053 y 013/014 como banco.

## 5. Invariantes

- El **layout** de `Node3D`/`Edge`/`Face`/`FaceIndex` y de `MeshAbi` se fija con `static_assert`
  (el asm y `obj2c` dependen de él).
- **Ningún byte crudo en la API de juego**: `MeshBlob::raw()` existe solo para el asm/procedencia.
- Todo acceso pasa por `Ref`/`Span`; todo cast byte→struct vive en `MeshBlob`.
