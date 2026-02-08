#pragma once

#include "GSRoot.hpp"
#include "GSGuid.hpp"
#include "UniString.hpp"
#include "DGModule.hpp"
#include "ResourceIDs.hpp"

namespace DG { class Browser; }

struct API_Neig;

class ToLayoutPalette : public DG::Palette, public DG::PanelObserver
{
public:
	static bool           HasInstance();
	static void           CreateInstance();
	static ToLayoutPalette& GetInstance();
	static void           DestroyInstance();

	static void           ShowPalette();
	static void           HidePalette();
	static void           UpdateSelectionListOnHTML();
	static GSErrCode      RegisterPaletteControlCallBack();

	virtual ~ToLayoutPalette();

private:
	ToLayoutPalette();

	void Init();
	void LoadHtml();

	static GSErrCode SelectionChangeHandler(const API_Neig* neig);

	void PanelResized(const DG::PanelResizeEvent& ev) override;
	void PanelCloseRequested(const DG::PanelCloseRequestEvent& ev, bool* accepted) override;

private:
	static GS::Ref<ToLayoutPalette> s_instance;
	static const GS::Guid           s_guid;

	DG::Browser* m_browserCtrl = nullptr;
};
