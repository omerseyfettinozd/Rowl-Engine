using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.Services.Workspace;

namespace RowlEngine.Editor.ViewModels
{
    /// <summary>
    /// Faz 8 C2 (yol haritası B05) — Kaydet / Farklı kaydet / Dışa aktar sonucu tek bir
    /// durum kartında: çalışıyor → başarılı (çıktı konumu + Klasörü aç) / hatalı (neden +
    /// Konsola git). Başarısız işlem asla başarı görünümü bırakmaz; düz kaydın başarısı
    /// kart açmaz (kirli nokta söner), yalnız hatası açar.
    /// </summary>
    public partial class MainWindowViewModel
    {
        private OperationStatusViewModel? _operationStatus;

        public OperationStatusViewModel OperationStatus
        {
            get
            {
                if (_operationStatus is null)
                {
                    _operationStatus = new OperationStatusViewModel();
                    _operationStatus.OpenFolderRequested += path =>
                        EditorProjectLifecycleCoordinator.OpenProjectFolder(path, AppendLog);
                    _operationStatus.ShowLogRequested += ShowConsole;
                }
                return _operationStatus;
            }
        }

        /// <summary>Konsol sekmesini açar ve seçer (açıksa kapatmaz — ShowPanel geçişlidir).</summary>
        private void ShowConsole()
        {
            IsLogPanelVisible = true;
            BottomPanelActiveTab = BottomStripState.LogTab;
        }

        /// <summary>Kayıt yazılamadı: hata kartı (kayıt yeniden başarılı olunca kendiliğinden kapanır).</summary>
        private void ReportSaveFailure(string reason) =>
            OperationStatus.Fail(OperationKind.Save,
                string.IsNullOrWhiteSpace(reason) ? EditorStrings.T("Operation.Save.WriteFailed") : reason);

        private void ReportSaveSuccess()
        {
            if (_operationStatus is { IsFailed: true, Kind: OperationKind.Save })
                _operationStatus.Dismiss();
        }
    }
}
