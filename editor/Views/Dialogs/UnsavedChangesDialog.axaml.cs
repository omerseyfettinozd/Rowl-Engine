using Avalonia.Controls;
namespace RowlEngine.Editor.Views.Dialogs;
public partial class UnsavedChangesDialog : Window
{
    public UnsavedChangesDialog()
    {
        InitializeComponent();
        this.FindControl<Button>("CancelButton")!.Click += (_, _) => Close("cancel");
        this.FindControl<Button>("DiscardButton")!.Click += (_, _) => Close("discard");
        this.FindControl<Button>("SaveButton")!.Click += (_, _) => Close("save");
    }
}
