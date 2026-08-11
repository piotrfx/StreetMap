#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/** Details customization that adds Generate/Clear buttons for UStreetMapBuildingDetailsComponent.
 *  UFUNCTION(CallInEditor) alone doesn't reliably surface a button for a plain native actor's root
 *  component when selected via the level Outliner, so this mirrors the same manual IDetailCustomization
 *  pattern StreetMapComponentDetails already uses for the "Build/Rebuild Mesh" button. */
class FStreetMapBuildingDetailsComponentDetails : public IDetailCustomization
{
public:
	FStreetMapBuildingDetailsComponentDetails();

	/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

	FReply OnGenerateClicked();
	FReply OnClearClicked();

protected:
	class UStreetMapBuildingDetailsComponent* SelectedComponent;
};
