using Avalonia.Input;

namespace RowlEngine.Editor.Services
{
    internal static class AssetDragData
    {
        internal static DataFormat<string> AssetPathFormat { get; } =
            DataFormat.CreateStringApplicationFormat("com.rowlengine.asset-path");

        internal static bool Contains(IDataTransfer data) => data.Contains(AssetPathFormat);

        internal static string? GetPath(IDataTransfer data) =>
            data.TryGetValue(AssetPathFormat);

        internal static DataTransfer Create(string assetPath)
        {
            var item = new DataTransferItem();
            item.Set(AssetPathFormat, assetPath);
            item.SetText(assetPath);

            var transfer = new DataTransfer();
            transfer.Add(item);
            return transfer;
        }
    }
}
