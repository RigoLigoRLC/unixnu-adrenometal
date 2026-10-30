# Shader input and output capture

The SM-T733 capture package carries
`/private/var/ngpu/addons/NativeAdrenoMetal.shadercapture`. Turnip reads this
marker when its process first creates a pipeline. It creates
`/private/var/mobile/Library/Caches/NativeAdrenoShaderCapture/<pid>/` and prefixes
every file with `s<monotonic-nanoseconds>_`. The prefix separates process runs
that reuse a PID. The USB network helper reads completed files on TCP 1236.

`metal-render-*.txt` records the runtime pipeline ID used in Metal workload
events, its frontend handle, original function names, constants with their
types and bytes, color formats, blend state, vertex layout and sample count.
`metal-compute-*.txt` carries the compute state ID and input descriptor.
`input-library-*.metallib` or `.air` holds the actual library bytes. Its filename
reuses the library identity already computed by the translator.

`runtime-pipeline-*.txt` links the compiled pipeline key and actual Vulkan
pipeline to Turnip's capture ID. It includes the runtime specialization and
translator strategy, precision and instruction summaries for render stages.
`driver-pipeline-*.txt` records that capture ID, VkPipeline, source modules,
entry names, specialization bytes, linked pipeline IDs, effective creation
flags, application cache feedback and KHR executable statistics.
`spirv-input-*.spv` is the exact code in each VkShaderModule passed to pipeline
creation, including runtime helper kernels. The module's existing identity
deduplicates these files. `driver-pipeline-*-stage-*-ir-*.txt` contains the KHR
initial NIR, final NIR and final IR3 assembly, named in the pipeline record.

Turnip adds `CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR` and
`CAPTURE_STATISTICS_BIT_KHR` to the caller's flags. Its existing compiler path
bypasses serialized driver shader results to retain these representations;
the NIR and IR3 optimization passes keep their normal settings. The runtime's
translation cache can still supply SPIR-V. Compilation and file export belong
outside a performance measurement window.

The shared writer publishes files by renaming `.part` files. A process session
allows 4095 artifact files plus one status file and at most 16 MiB per artifact.
Input libraries have a 32 MiB budget, SPIR-V and runtime metadata 32 MiB, and
driver output 64 MiB. `status.json` records bytes, written files, refusals,
write failures and the latest reason. An input record says whether its library
was saved; each driver IR entry says whether that representation was saved.
No draw or dispatch writes these files after its pipeline has been created.

Match workload events and dumps from the same process run. Their runtime ID
links the original descriptor, and their VkPipeline links the actual submitted
pipeline to its capture ID and assembly. File identities from an earlier boot
are insufficient to assign a later run's pipeline addresses.
