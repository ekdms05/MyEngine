/*
 * MyEngine vendored FreeType — trimmed module registry.
 *
 * Only the modules whose source dirs we keep and compile in
 * third_party/CMakeLists.txt (mye_thirdparty_freetype) are listed here.
 * Removed vs. upstream default: pfr, winfnt, pcf, bdf, sdf renderers, svg.
 * Kept: autofit, TrueType, Type1, CFF, CID, Type42, psaux/psnames/pshinter,
 *       sfnt, smooth + raster renderers.  (docs/06 텍스트 렌더링 — TTF/OTF 한글 폰트)
 */

FT_USE_MODULE( FT_Module_Class, autofit_module_class )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, t1_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, t1cid_driver_class )
FT_USE_MODULE( FT_Driver_ClassRec, t42_driver_class )
FT_USE_MODULE( FT_Module_Class, psaux_module_class )
FT_USE_MODULE( FT_Module_Class, psnames_module_class )
FT_USE_MODULE( FT_Module_Class, pshinter_module_class )
FT_USE_MODULE( FT_Module_Class, sfnt_module_class )
FT_USE_MODULE( FT_Renderer_Class, ft_smooth_renderer_class )
FT_USE_MODULE( FT_Renderer_Class, ft_raster1_renderer_class )

/* EOF */
