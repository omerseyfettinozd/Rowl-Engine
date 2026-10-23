using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.Services.Inspector;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using Xunit;

namespace RowlEngine.Editor.Tests
{
    // Saf çekirdek testleri (UI yok).
    public sealed class EditorInspectorUndoTests
    {
        private sealed class FakeSink : IUndoActionSink
        {
            public readonly List<IUndoAction> Actions = new();
            public void RecordAction(IUndoAction a) => Actions.Add(a);
        }

        private static PropertyInfo FindDialogueProp(object vm) =>
            vm.GetType().GetProperty(nameof(DialogueComponentViewModel.DialogueText))!;

        [Fact]
        public void Session_TitleEdits_CoalesceToSingleAction()
        {
            var node = new NodeViewModel(1, "A", 0, 0, bare: true);
            var sink = new FakeSink();
            using var session = new InspectorEditSession(() => false);
            session.Track(node);
            session.Begin();
            node.Title = "AB";
            node.Title = "ABC";
            session.Commit(sink);

            var single = Assert.Single(sink.Actions);
            Assert.Equal("Edit Title", single.Description);
            single.Undo();
            Assert.Equal("A", node.Title);
            single.Redo();
            Assert.Equal("ABC", node.Title);
        }

        [Fact]
        public void Session_InactiveChange_RecordsNothing_ButAdvancesSnapshot()
        {
            var node = new NodeViewModel(2, "A", 0, 0, bare: true);
            var sink = new FakeSink();
            using var session = new InspectorEditSession(() => false);
            session.Track(node);
            // Begin yok: yalnız görüntü ilerler.
            node.Title = "B";
            session.Commit(sink);
            Assert.Empty(sink.Actions);

            // Sonraki oturum bazı B olmalı.
            session.Begin();
            node.Title = "C";
            session.Commit(sink);
            var single = Assert.Single(sink.Actions);
            single.Undo();
            Assert.Equal("B", node.Title);
            single.Redo();
            Assert.Equal("C", node.Title);
        }

        [Fact]
        public void Session_RevertedEdit_ProducesNoAction()
        {
            var node = new NodeViewModel(3, "A", 0, 0, bare: true);
            var sink = new FakeSink();
            using var session = new InspectorEditSession(() => false);
            session.Track(node);
            session.Begin();
            node.Title = "B";
            node.Title = "A";
            session.Commit(sink);
            Assert.Empty(sink.Actions);
        }

        [Fact]
        public void Session_ExcludedProps_NeverRecorded()
        {
            var node = new NodeViewModel(4, "A", 0, 0, bare: true);
            var sink = new FakeSink();
            using var session = new InspectorEditSession(() => false);
            session.Track(node);
            session.Begin();
            node.X = 123.0;
            node.IsSelected = true;
            session.Commit(sink);
            Assert.Empty(sink.Actions);
        }

        [Fact]
        public void Session_ComponentAndOptionText_Tracked_IncludingAddedOption()
        {
            var node = new NodeViewModel(10, "N", 0, 0, bare: true);
            var frame = new FrameObjectViewModel { Name = "Obj" };
            node.Objects.Add(frame);
            var dlg = new DialogueComponentViewModel();
            frame.Components.Add(dlg);
            var choice = new ChoiceComponentViewModel();
            frame.Components.Add(choice);

            var dlgProp = FindDialogueProp(dlg);
            var dlgBefore = (string?)dlgProp.GetValue(dlg);
            var existing = choice.Options[0];
            var existingBefore = existing.Text;

            var sink = new FakeSink();
            using var session = new InspectorEditSession(() => false);
            session.Track(node);
            session.Begin();
            dlgProp.SetValue(dlg, "Hello");
            existing.Text = "OptA";
            var added = new ChoiceOptionViewModel { Text = "Temp" };
            choice.Options.Add(added);
            added.Text = "Added";
            session.Commit(sink);

            var single = Assert.Single(sink.Actions);
            single.Undo();
            Assert.Equal(dlgBefore, (string?)dlgProp.GetValue(dlg));
            Assert.Equal(existingBefore, existing.Text);
            Assert.Equal("Temp", added.Text);
            single.Redo();
            Assert.Equal("Hello", (string?)dlgProp.GetValue(dlg));
            Assert.Equal("OptA", existing.Text);
            Assert.Equal("Added", added.Text);
        }

        [Fact]
        public void Session_ExecutingChanges_ProduceNoRecord()
        {
            var node = new NodeViewModel(20, "A", 0, 0, bare: true);
            bool executing = false;
            using var session = new InspectorEditSession(() => executing);
            var sink = new FakeSink();
            session.Track(node);
            session.Begin();
            executing = true;
            node.Title = "B";
            executing = false;
            node.Title = "C";
            session.Commit(sink);

            var single = Assert.Single(sink.Actions);
            Assert.Equal("Edit Title", single.Description);
            single.Undo();
            Assert.Equal("B", node.Title);
            single.Redo();
            Assert.Equal("C", node.Title);
        }

        [Fact]
        public void Session_Untrack_RemovesSubscriptions()
        {
            var node = new NodeViewModel(30, "A", 0, 0, bare: true);
            var sink = new FakeSink();
            using var session = new InspectorEditSession(() => false);
            session.Track(node);
            session.Untrack();
            session.Begin();
            node.Title = "B";
            session.Commit(sink);
            Assert.Empty(sink.Actions);
        }

        [Fact]
        public void Session_DialogueBoxLayoutFields_AreRecorded()
        {
            // X/Y yalnız düğüm kartında dışlanır; diyalog kutusu konumu yazar ayarıdır.
            var node = new NodeViewModel(11, "N", 0, 0, bare: true);
            var frame = new FrameObjectViewModel { Name = "Obj" };
            node.Objects.Add(frame);
            var dlg = new DialogueComponentViewModel();
            frame.Components.Add(dlg);
            double before = dlg.X;

            var sink = new FakeSink();
            using var session = new InspectorEditSession(() => false);
            session.Track(node);
            session.Begin();
            dlg.X = before + 40;
            node.X = 500; // düğüm kartı konumu kaydedilmez
            session.Commit(sink);

            var single = Assert.Single(sink.Actions);
            single.Undo();
            Assert.Equal(before, dlg.X);
            Assert.Equal(500, node.X);
        }
    }
}
