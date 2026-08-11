#include "StreetMapImporting.h"
#include "StreetMapAssetTypeActions.h"

#include "AssetToolsModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "StreetMapComponentDetails.h"
#include "StreetMapBuildingDetailsComponentDetails.h"
#include "StreetMapStyle.h"

class FStreetMapImportingModule : public IModuleInterface
{

public:

	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedPtr< FStreetMapAssetTypeActions > StreetMapAssetTypeActions;
};


IMPLEMENT_MODULE( FStreetMapImportingModule, StreetMapImporting )



void FStreetMapImportingModule::StartupModule()
{
	// Register asset types
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>( "AssetTools" ).Get();
	StreetMapAssetTypeActions = MakeShareable( new FStreetMapAssetTypeActions() );
	AssetTools.RegisterAssetTypeActions( StreetMapAssetTypeActions.ToSharedRef() );

	// Initialize & Register StreetMap Style
	FStreetMapStyle::Initialize();

	// Register StreetMapComponent Detail Customization
	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomClassLayout("StreetMapComponent", FOnGetDetailCustomizationInstance::CreateStatic(&FStreetMapComponentDetails::MakeInstance));
	PropertyModule.RegisterCustomClassLayout("StreetMapBuildingDetailsComponent", FOnGetDetailCustomizationInstance::CreateStatic(&FStreetMapBuildingDetailsComponentDetails::MakeInstance));
	PropertyModule.NotifyCustomizationModuleChanged();
}


void FStreetMapImportingModule::ShutdownModule()
{
	// Unregister all the asset types that we registered
	if( FModuleManager::Get().IsModuleLoaded( "AssetTools" ) )
	{
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>( "AssetTools" ).Get();
		AssetTools.UnregisterAssetTypeActions( StreetMapAssetTypeActions.ToSharedRef() );
		StreetMapAssetTypeActions.Reset();
	}

	// Unregister StreetMap Style
	FStreetMapStyle::Shutdown();

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout("StreetMapComponent");
		PropertyModule.UnregisterCustomClassLayout("StreetMapBuildingDetailsComponent");
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}
