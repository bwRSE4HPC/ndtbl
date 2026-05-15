
.. toctree::
   :maxdepth: 2
   :caption: Contents:

API reference
-------------

.. doxygenclass:: ndtbl::Axis
   :members:

.. doxygenclass:: ndtbl::Grid
   :members:

.. doxygenclass:: ndtbl::TensorStencil
   :members:

.. doxygentypedef:: ndtbl::LinearStencil

.. doxygentypedef:: ndtbl::CubicStencil

.. doxygenclass:: ndtbl::FieldGroup
   :members:

.. doxygenclass:: ndtbl::RuntimeFieldGroup
   :members:

.. doxygenstruct:: ndtbl::GroupMetadata
   :members:

.. doxygenenum:: ndtbl::axis_kind

.. doxygenenum:: ndtbl::scalar_type

.. doxygenenum:: ndtbl::bounds_policy

.. doxygenfunction:: ndtbl::read_group_metadata(const std::string &path)

.. doxygenfunction:: ndtbl::read_field_group(const std::string &path)

.. doxygenfunction:: ndtbl::read_runtime_field_group(const std::string &path)
