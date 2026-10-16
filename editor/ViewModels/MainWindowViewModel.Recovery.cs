using System.Linq;
using System.Threading.Tasks;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>
    /// Faz 8 B (yol haritası B04) — kurtarma ve içe aktarmanın ana pencere tarafı: açılışta
    /// kurtarma teklifi kart olarak gelir (incele / geri yükle / yoksay; yoksay dosyaları
    /// silmez), içe aktarma UI iş parçacığı dışında ilerleme + iptal + çakışma kararıyla koşar.
    /// </summary>
    public partial class MainWindowViewModel
    {
        private AssetImportViewModel? _assetImport;

        /// <summary>Menüden içe aktarma oturumu (ilerleme, iptal, aynı adlı varlık kararı).</summary>
        public AssetImportViewModel AssetImport => _assetImport ??= new AssetImportViewModel();

        /// <summary>Açılışta bekleyen kurtarma teklifi kartı; yoksa null.</summary>
        [ObservableProperty]
        private RecoveryOfferViewModel? _recoveryOffer;

        private void OfferRecovery(RestoreOffer offer, string? canonicalError)
        {
            PendingRecoveryOffer = offer;
            RecoveryOffer = new RecoveryOfferViewModel(
                offer,
                canonicalError,
                restore: () =>
                {
                    RestoreFromRecovery();
                    bool restored = PendingRecoveryOffer is null;
                    if (restored)
                        RecoveryOffer = null;
                    return restored;
                },
                dismiss: () =>
                {
                    // Kurtarma dosyaları silinmez; teklif yalnız bu oturumda kapanır.
                    RecoveryOffer = null;
                    AppendLog("Kurtarma teklifi bu oturum için yoksayıldı (kurtarma dosyaları korunuyor).");
                });
        }

        [RelayCommand]
        public async Task ImportAssetAsync()
        {
            try
            {
                if (EditorDialogService.GetMainWindow() == null) return;

                var files = await EditorDialogService.PickAssetFilesAsync();
                if (files == null || files.Count == 0) return;

                var filePaths = files.Select(f => f.Path.LocalPath).ToList();
                await AssetImport.RunAsync(filePaths, AssetsPath, AppendLog);
                AssetBrowserViewModel.RefreshAssets();
            }
            catch (System.Exception ex)
            {
                AppendLog($"Failed to import asset: {ex.Message}");
            }
        }
    }
}
