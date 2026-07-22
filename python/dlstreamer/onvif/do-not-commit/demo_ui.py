#!/usr/bin/env python3
"""
ONVIF Camera & Pipeline Management UI - Demo
A sample GUI showing camera discovery, profiles, pipelines, and event-based triggers.
Built with tkinter (no external dependencies required).

Usage:
    python3 demo_ui.py
"""

import tkinter as tk
from tkinter import ttk
from datetime import datetime


class CameraProfileManagerUI:
    """Demo UI for ONVIF camera and pipeline management."""

    # Mock data (in production, would come from ONVIF library and config)
    MOCK_CAMERAS = [
        {
            "id": "camera_1",
            "hostname": "10.91.106.65",
            "port": 2020,
            "name": "Front Gate Camera",
            "status": "Online",
        },
        {
            "id": "camera_2",
            "hostname": "192.168.1.100",
            "port": 80,
            "name": "Warehouse Camera",
            "status": "Online",
        },
        {
            "id": "camera_3",
            "hostname": "192.168.1.101",
            "port": 80,
            "name": "Parking Lot Camera",
            "status": "Offline",
        },
    ]

    MOCK_PROFILES = {
        "camera_1": [
            {
                "token": "profile_1",
                "name": "High Resolution",
                "resolution": "1920x1080",
                "encoding": "H.264",
            },
            {
                "token": "profile_2",
                "name": "Low Bandwidth",
                "resolution": "640x480",
                "encoding": "H.264",
            },
        ],
        "camera_2": [
            {
                "token": "profile_3",
                "name": "Standard",
                "resolution": "1280x720",
                "encoding": "H.264",
            },
        ],
        "camera_3": [],
    }

    MOCK_CAMERA_PIPELINE_BINDINGS = [
        {
            "id": "binding_1",
            "camera": "Front Gate Camera (10.91.106.65)",
            "pipeline": "motion_detection_pipeline",
            "profile": "High Resolution",
            "active": True,
        },
        {
            "id": "binding_2",
            "camera": "Warehouse Camera (192.168.1.100)",
            "pipeline": "person_detection_pipeline",
            "profile": "Standard",
            "active": True,
        },
        {
            "id": "binding_3",
            "camera": "Parking Lot Camera (192.168.1.101)",
            "pipeline": "vehicle_tracking_pipeline",
            "profile": "Low Bandwidth",
            "active": False,
        },
    ]

    MOCK_PIPELINES = [
        {"name": "motion_detection_pipeline", "type": "analytic", "status": "Loaded"},
        {"name": "person_detection_pipeline", "type": "analytic", "status": "Loaded"},
        {
            "name": "vehicle_tracking_pipeline",
            "type": "analytic",
            "status": "Not loaded",
        },
        {"name": "face_recognition_pipeline", "type": "analytic", "status": "Loaded"},
    ]

    MOCK_EVENT_PIPELINES = [
        {
            "name": "motion_alert_pipeline",
            "trigger_event": "tns1:RuleEngine/CellMotionDetector/Motion",
            "action": "Send email & record",
            "enabled": True,
        },
        {
            "name": "intrusion_alert_pipeline",
            "trigger_event": "tns1:RuleEngine/IntrusionDetector/Intrusion",
            "action": "Send SMS & trigger alarm",
            "enabled": True,
        },
        {
            "name": "tamper_detection_pipeline",
            "trigger_event": "tns1:RuleEngine/TamperDetector/Tamper",
            "action": "Log incident",
            "enabled": False,
        },
    ]

    def __init__(self, root):
        self.root = root
        self.root.title("ONVIF Camera & Pipeline Manager - Demo")
        self.root.geometry("1200x700")
        self.root.configure(bg="#f0f0f0")

        self.selected_camera = None
        self.selected_profile = None

        self._create_ui()

    def _create_ui(self):
        """Build the main UI with tabs."""
        style = ttk.Style()
        style.theme_use("clam")

        # Header
        header_frame = tk.Frame(self.root, bg="#2c3e50", height=50)
        header_frame.pack(fill=tk.X, padx=0, pady=0)
        header_label = tk.Label(
            header_frame,
            text="ONVIF Camera & Pipeline Management",
            bg="#2c3e50",
            fg="white",
            font=("Arial", 14, "bold"),
            pady=10,
        )
        header_label.pack()

        # Notebook (tabs)
        notebook = ttk.Notebook(self.root)
        notebook.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        # Tab 1: Cameras & Profiles
        self._create_cameras_tab(notebook)

        # Tab 2: Camera-Pipeline Bindings
        self._create_bindings_tab(notebook)

        # Tab 3: Pipelines
        self._create_pipelines_tab(notebook)

        # Tab 4: Event Pipelines
        self._create_event_pipelines_tab(notebook)

        # Footer
        footer_frame = tk.Frame(self.root, bg="#ecf0f1", height=30)
        footer_frame.pack(fill=tk.X, padx=0, pady=0)
        footer_label = tk.Label(
            footer_frame,
            text=f"Demo UI | Last updated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
            bg="#ecf0f1",
            fg="#7f8c8d",
            font=("Arial", 9),
            pady=5,
        )
        footer_label.pack()

    def _create_cameras_tab(self, notebook):
        """Tab 1: Camera discovery and profile details."""
        frame = ttk.Frame(notebook)
        notebook.add(frame, text="Cameras & Profiles")

        # Left panel: Camera list
        left_frame = ttk.Frame(frame)
        left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=False, padx=5, pady=5)

        tk.Label(left_frame, text="Discovered Cameras", font=("Arial", 10, "bold")).pack(
            anchor=tk.W
        )
        self.camera_tree = ttk.Treeview(
            left_frame, height=15, columns=("status",)
        )
        self.camera_tree.heading("#0", text="Camera")
        self.camera_tree.heading("status", text="Status")
        self.camera_tree.column("#0", width=180)
        self.camera_tree.column("status", width=80)
        self.camera_tree.pack(fill=tk.BOTH, expand=True)
        self.camera_tree.bind("<<TreeviewSelect>>", self._on_camera_select)

        # Populate cameras
        for cam in self.MOCK_CAMERAS:
            status_color = "🟢" if cam["status"] == "Online" else "🔴"
            self.camera_tree.insert(
                "",
                "end",
                cam["id"],
                text=f"{cam['name']}",
                values=(f"{status_color} {cam['status']}",),
            )

        # Right panel: Profiles and details
        right_frame = ttk.Frame(frame)
        right_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=5, pady=5)

        tk.Label(right_frame, text="Camera Details", font=("Arial", 10, "bold")).pack(
            anchor=tk.W
        )
        self.camera_details_text = tk.Text(right_frame, height=8, width=50, wrap=tk.WORD)
        self.camera_details_text.pack(fill=tk.X, pady=5)
        self.camera_details_text.config(state=tk.DISABLED)

        tk.Label(right_frame, text="Profiles", font=("Arial", 10, "bold")).pack(
            anchor=tk.W, pady=(10, 0)
        )
        self.profile_tree = ttk.Treeview(
            right_frame, height=12, columns=("encoding", "resolution")
        )
        self.profile_tree.heading("#0", text="Profile Name")
        self.profile_tree.heading("resolution", text="Resolution")
        self.profile_tree.heading("encoding", text="Encoding")
        self.profile_tree.column("#0", width=180)
        self.profile_tree.column("resolution", width=100)
        self.profile_tree.column("encoding", width=80)
        self.profile_tree.pack(fill=tk.BOTH, expand=True)
        self.profile_tree.bind("<<TreeviewSelect>>", self._on_profile_select)

    def _create_bindings_tab(self, notebook):
        """Tab 2: Camera-Pipeline static bindings."""
        frame = ttk.Frame(notebook)
        notebook.add(frame, text="Camera-Pipeline Bindings")

        tk.Label(frame, text="Static Camera-Pipeline Associations", font=("Arial", 10, "bold")).pack(
            anchor=tk.W, padx=10, pady=5
        )

        self.bindings_tree = ttk.Treeview(
            frame,
            columns=("camera", "pipeline", "profile", "active"),
            height=20,
        )
        self.bindings_tree.heading("#0", text="ID")
        self.bindings_tree.heading("camera", text="Camera")
        self.bindings_tree.heading("pipeline", text="Pipeline")
        self.bindings_tree.heading("profile", text="Profile")
        self.bindings_tree.heading("active", text="Active")

        self.bindings_tree.column("#0", width=80)
        self.bindings_tree.column("camera", width=250)
        self.bindings_tree.column("pipeline", width=200)
        self.bindings_tree.column("profile", width=150)
        self.bindings_tree.column("active", width=80)

        for binding in self.MOCK_CAMERA_PIPELINE_BINDINGS:
            status = "✓" if binding["active"] else "✗"
            self.bindings_tree.insert(
                "",
                "end",
                binding["id"],
                text=binding["id"],
                values=(binding["camera"], binding["pipeline"], binding["profile"], status),
            )

        self.bindings_tree.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

    def _create_pipelines_tab(self, notebook):
        """Tab 3: Defined pipelines."""
        frame = ttk.Frame(notebook)
        notebook.add(frame, text="Pipelines")

        tk.Label(frame, text="Available Analytics Pipelines", font=("Arial", 10, "bold")).pack(
            anchor=tk.W, padx=10, pady=5
        )

        self.pipelines_tree = ttk.Treeview(
            frame, columns=("type", "status"), height=20
        )
        self.pipelines_tree.heading("#0", text="Pipeline Name")
        self.pipelines_tree.heading("type", text="Type")
        self.pipelines_tree.heading("status", text="Status")

        self.pipelines_tree.column("#0", width=250)
        self.pipelines_tree.column("type", width=150)
        self.pipelines_tree.column("status", width=150)

        for pipeline in self.MOCK_PIPELINES:
            status_icon = "✓" if pipeline["status"] == "Loaded" else "✗"
            self.pipelines_tree.insert(
                "",
                "end",
                pipeline["name"],
                text=pipeline["name"],
                values=(pipeline["type"], f"{status_icon} {pipeline['status']}"),
            )

        self.pipelines_tree.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

    def _create_event_pipelines_tab(self, notebook):
        """Tab 4: Event-triggered pipelines."""
        frame = ttk.Frame(notebook)
        notebook.add(frame, text="Event Pipelines")

        tk.Label(frame, text="Event-Triggered Pipelines", font=("Arial", 10, "bold")).pack(
            anchor=tk.W, padx=10, pady=5
        )

        self.event_pipelines_tree = ttk.Treeview(
            frame, columns=("trigger_event", "action", "enabled"), height=20
        )
        self.event_pipelines_tree.heading("#0", text="Pipeline Name")
        self.event_pipelines_tree.heading("trigger_event", text="Trigger Event")
        self.event_pipelines_tree.heading("action", text="Action")
        self.event_pipelines_tree.heading("enabled", text="Enabled")

        self.event_pipelines_tree.column("#0", width=200)
        self.event_pipelines_tree.column("trigger_event", width=300)
        self.event_pipelines_tree.column("action", width=200)
        self.event_pipelines_tree.column("enabled", width=80)

        for ep in self.MOCK_EVENT_PIPELINES:
            enabled_icon = "✓" if ep["enabled"] else "✗"
            self.event_pipelines_tree.insert(
                "",
                "end",
                ep["name"],
                text=ep["name"],
                values=(ep["trigger_event"], ep["action"], f"{enabled_icon}"),
            )

        self.event_pipelines_tree.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

    def _on_camera_select(self, event):
        """Handle camera selection."""
        selection = self.camera_tree.selection()
        if not selection:
            return

        camera_id = selection[0]
        self.selected_camera = camera_id

        # Find camera data
        camera = next((c for c in self.MOCK_CAMERAS if c["id"] == camera_id), None)
        if not camera:
            return

        # Update camera details
        details = (
            f"Name: {camera['name']}\n"
            f"Hostname: {camera['hostname']}\n"
            f"Port: {camera['port']}\n"
            f"Status: {camera['status']}\n"
            f"ID: {camera['id']}"
        )
        self.camera_details_text.config(state=tk.NORMAL)
        self.camera_details_text.delete(1.0, tk.END)
        self.camera_details_text.insert(1.0, details)
        self.camera_details_text.config(state=tk.DISABLED)

        # Update profiles
        self.profile_tree.delete(*self.profile_tree.get_children())
        profiles = self.MOCK_PROFILES.get(camera_id, [])
        for profile in profiles:
            self.profile_tree.insert(
                "",
                "end",
                profile["token"],
                text=profile["name"],
                values=(profile["resolution"], profile["encoding"]),
            )

    def _on_profile_select(self, event):
        """Handle profile selection."""
        selection = self.profile_tree.selection()
        if not selection:
            return

        profile_token = selection[0]
        self.selected_profile = profile_token


def main():
    """Launch the demo UI."""
    root = tk.Tk()
    app = CameraProfileManagerUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
