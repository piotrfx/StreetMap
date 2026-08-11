#include "StreetMapBuildingDetailsComponentDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "GameFramework/Actor.h"
#include "IDetailsView.h"
#include "StreetMapBuildingDetailsComponent.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "StreetMapBuildingDetailsComponentDetails"


FStreetMapBuildingDetailsComponentDetails::FStreetMapBuildingDetailsComponentDetails() :
	SelectedComponent(nullptr)
{
}

TSharedRef<IDetailCustomization> FStreetMapBuildingDetailsComponentDetails::MakeInstance()
{
	return MakeShareable(new FStreetMapBuildingDetailsComponentDetails());
}

void FStreetMapBuildingDetailsComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjects = DetailBuilder.GetDetailsView()->GetSelectedObjects();

	for (const TWeakObjectPtr<UObject>& Object : SelectedObjects)
	{
		if (UStreetMapBuildingDetailsComponent* Comp = Cast<UStreetMapBuildingDetailsComponent>(Object.Get()))
		{
			if (!Comp->IsTemplate())
			{
				SelectedComponent = Comp;
				break;
			}
		}
	}

	if (SelectedComponent == nullptr)
	{
		for (const TWeakObjectPtr<UObject>& Object : SelectedObjects)
		{
			if (AActor* TempActor = Cast<AActor>(Object.Get()))
			{
				if (!TempActor->IsTemplate())
				{
					if (UStreetMapBuildingDetailsComponent* Comp = TempActor->FindComponentByClass<UStreetMapBuildingDetailsComponent>())
					{
						if (!Comp->IsTemplate())
						{
							SelectedComponent = Comp;
						}
					}
				}
				break;
			}
		}
	}

	if (SelectedComponent == nullptr)
	{
		return;
	}

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("StreetMap", FText::GetEmpty(), ECategoryPriority::Important);
	Category.InitiallyCollapsed(false);

	TSharedPtr<SHorizontalBox> Row;
	Category.AddCustomRow(FText::GetEmpty(), false)
	[
		SAssignNew(Row, SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("Generate_Tooltip", "Clears and repopulates window/door instances from the current StreetMap data and tunables."))
			.OnClicked(this, &FStreetMapBuildingDetailsComponentDetails::OnGenerateClicked)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Generate", "Generate Building Details"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
	];

	Row->AddSlot()
	[
		SNew(SButton)
		.ToolTipText(LOCTEXT("Clear_Tooltip", "Removes all placed window/door instances without regenerating."))
		.OnClicked(this, &FStreetMapBuildingDetailsComponentDetails::OnClearClicked)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Clear", "Clear Building Details"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
	];
}

FReply FStreetMapBuildingDetailsComponentDetails::OnGenerateClicked()
{
	if (SelectedComponent != nullptr)
	{
		SelectedComponent->GenerateBuildingDetails();
	}
	return FReply::Handled();
}

FReply FStreetMapBuildingDetailsComponentDetails::OnClearClicked()
{
	if (SelectedComponent != nullptr)
	{
		SelectedComponent->ClearBuildingDetails();
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
