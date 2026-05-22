using UnrealBuildTool;

public class FactoryMonitor : ModuleRules
{
    public FactoryMonitor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // ----------------------------------------------------------------
        // Dépendances publiques
        // ----------------------------------------------------------------
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",

            // Module principal de Satisfactory — fournit toutes les
            // classes AFG*/UFG* utilisées dans le .cpp.
            "FactoryGame",

            // SML Runtime — nécessaire pour l'enregistrement du
            // sous-système et les utilitaires SML.
            "SML",

            // Client HTTP outbound (POST uniquement — aucun port local ouvert).
            "HTTP",

            // Modèle objet JSON + sérialiseur.
            "Json",
            "JsonUtilities",
        });

        // ----------------------------------------------------------------
        // Dépendances privées (utilisées uniquement dans les .cpp)
        // ----------------------------------------------------------------
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            // Slate est tiré transitivement par FactoryGame sur certaines
            // configurations ; on le déclare explicitement pour éviter les
            // avertissements de liaison.
            "Slate",
            "SlateCore",
        });

        // Convention SML : pas d'exceptions C++.
        bEnableExceptions = false;
    }
}
