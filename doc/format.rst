Format
======

The ``.ndtbl`` format stores one multidimensional field group in a compact binary layout with self-describing metadata followed by one contiguous payload block.

On-disk encoding
----------------

All integers and floating-point values are encoded in little-endian byte order.
Floating-point values use IEEE-754 ``binary32`` and ``binary64``.

Header
------

Each file begins with this fixed header:

* ``magic[8]``: ``"NDTBL\0\0\0"``
* ``version:u8``: currently ``1``
* ``scalar_type:u8``: ``1=float32``, ``2=float64``
* ``reserved:u16``: must be zero
* ``payload_offset:u64``: byte offset of the first payload value
* ``dimension:u64``: number of axes
* ``field_count:u64``: number of named fields per grid point
* ``point_count:u64``: total number of grid points

Axis records
------------

One axis record is stored for each axis in axis order:

* ``kind:u8``: ``1=uniform``, ``2=explicit_coordinates``
* ``reserved0:u8``: must be zero
* ``reserved1:u16``: must be zero
* ``extent:u64``: number of points on the axis

Uniform axes append:

* ``min:f64``
* ``max:f64``

Explicit-coordinate axes append:

* ``extent`` repeated ``f64`` coordinates

Field names
-----------

Field names are written in payload order as:

* ``name_length:u64``
* ``name_bytes[name_length]`` encoded as UTF-8

Payload ordering
----------------

The payload stores raw scalar values in point-major order.

Logical point traversal follows row-major / C-order over the grid axes:

* axis ``0`` is the slowest-varying grid index
* the last axis is the fastest-varying grid index
* for each logical point, all field values are written consecutively in ``field_names`` order

Equivalently, the payload is the flattened form of an array shaped as ``(*axis_sizes, field_count)`` in C-order.

Validation rules
----------------

Readers should reject files when:

* reserved fields are nonzero
* ``point_count`` does not match the product of axis extents
* ``payload_offset`` does not match the parsed metadata length
* an axis record, explicit coordinate array, or field name crosses ``payload_offset``
* encoded counts or lengths cannot be represented safely or are not backed by enough bytes in the declared metadata or payload regions
* the payload does not end exactly at the end of the file
* encoded sizes exceed supported host limits
