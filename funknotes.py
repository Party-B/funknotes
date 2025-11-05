#!/usr/bin/env python3
"""
FunkNotes - It's a note app for the command line, so you can keep track of projects without leaving the dank CLI.
"""

import json
import os
import sys
from datetime import datetime
from pathlib import Path
import argparse


class FunkNotesConfig:
    """Manages FunkNotes configuration and storage"""
    
    def __init__(self):
        self.home = Path.home() / ".funknotes"
        self.home.mkdir(exist_ok=True)
        self.config_file = self.home / "config.json"
        self.projects_dir = self.home / "projects"
        self.projects_dir.mkdir(exist_ok=True)
        
    def load_config(self):
        if self.config_file.exists():
            with open(self.config_file, 'r') as f:
                return json.load(f)
        return {"primary_project": None, "project_counter": 0}
    
    def save_config(self, config):
        with open(self.config_file, 'w') as f:
            json.dump(config, f, indent=2)


class FunkNotesProject:
    """Represents a single FunkNotes project"""
    
    def __init__(self, name, index, config):
        self.name = name
        self.index = index
        self.config = config
        self.project_file = config.projects_dir / f"{index}_{name}.json"
        self.data = self._load_or_create()
    
    def _load_or_create(self):
        if self.project_file.exists():
            with open(self.project_file, 'r') as f:
                return json.load(f)
        return {
            "name": self.name,
            "index": self.index,
            "objects": {},
            "commits": []
        }
    
    def save(self):
        with open(self.project_file, 'w') as f:
            json.dump(self.data, f, indent=2)
    
    def add_object(self, object_name):
        if object_name not in self.data["objects"]:
            self.data["objects"][object_name] = {
                "items": [],
                "history": []
            }
            self.save()
            return True
        return False
    
    def add_item(self, object_name, item_text):
        if object_name not in self.data["objects"]:
            raise ValueError(f"Object '{object_name}' does not exist")
        
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        item_data = {
            "timestamp": timestamp,
            "text": item_text
        }
        
        self.data["objects"][object_name]["items"].append(item_data)
        self.data["objects"][object_name]["history"].append({
            "action": "ADD",
            "timestamp": timestamp,
            "text": item_text
        })
        self.save()
    
    def commit(self, message):
        commit_data = {
            "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "message": message
        }
        self.data["commits"].append(commit_data)
        self.save()
    
    def show_objects(self):
        if not self.data["objects"]:
            print(f"No objects in project '{self.name}'")
            return
        
        print(f"\n=== Objects in '{self.name}' ===")
        for obj_name in self.data["objects"].keys():
            item_count = len(self.data["objects"][obj_name]["items"])
            print(f"  • {obj_name} ({item_count} items)")
    
    def show_items(self, object_name):
        if object_name not in self.data["objects"]:
            print(f"Object '{object_name}' not found")
            return
        
        items = self.data["objects"][object_name]["items"]
        if not items:
            print(f"\n=== {object_name} (empty) ===")
            return
        
        print(f"\n=== {object_name} ===")
        for i, item in enumerate(items, 1):
            print(f"{i}. [{item['timestamp']}] {item['text']}")
    
    def show_history(self, object_name):
        if object_name not in self.data["objects"]:
            print(f"Object '{object_name}' not found")
            return
        
        history = self.data["objects"][object_name]["history"]
        if not history:
            print(f"\n=== {object_name} History (empty) ===")
            return
        
        print(f"\n=== {object_name} History ===")
        for entry in history:
            print(f"[{entry['timestamp']}] {entry['action']}: {entry['text']}")


class FunkNotes:
    """Main FunkNotes system"""
    
    def __init__(self):
        self.config_manager = FunkNotesConfig()
        self.config = self.config_manager.load_config()
    
    def new_project(self, project_name):
        self.config["project_counter"] += 1
        index = self.config["project_counter"]
        
        project = FunkNotesProject(project_name, index, self.config_manager)
        project.save()
        
        self.config_manager.save_config(self.config)
        
        print(f"Created project '{project_name}' with index {index}")
        return index
    
    def set_primary(self, project_ref):
        project = self._get_project(project_ref)
        if project:
            self.config["primary_project"] = project.index
            self.config_manager.save_config(self.config)
            print(f"Set primary project to '{project.name}'")
        else:
            print(f"Project '{project_ref}' not found")
    
    def _get_project(self, project_ref):
        # Try to parse as index
        try:
            index = int(project_ref)
            for file in self.config_manager.projects_dir.glob("*.json"):
                proj = FunkNotesProject("temp", 0, self.config_manager)
                proj.project_file = file
                proj.data = proj._load_or_create()
                if proj.data["index"] == index:
                    proj.name = proj.data["name"]
                    proj.index = proj.data["index"]
                    return proj
        except ValueError:
            # Try as name
            for file in self.config_manager.projects_dir.glob("*.json"):
                proj = FunkNotesProject("temp", 0, self.config_manager)
                proj.project_file = file
                proj.data = proj._load_or_create()
                if proj.data["name"] == project_ref:
                    proj.name = proj.data["name"]
                    proj.index = proj.data["index"]
                    return proj
        return None
    
    def get_primary_project(self):
        if not self.config["primary_project"]:
            print("No primary project set. Use 'funknotes primary <project>' first.")
            return None
        return self._get_project(self.config["primary_project"])
    
    def add_object(self, object_name):
        project = self.get_primary_project()
        if project:
            if project.add_object(object_name):
                print(f"Created object '{object_name}' in project '{project.name}'")
            else:
                print(f"Object '{object_name}' already exists")
    
    def add_item(self, object_name, item_text):
        project = self.get_primary_project()
        if project:
            try:
                project.add_item(object_name, item_text)
                print(f"Added item to {object_name}")
            except ValueError as e:
                print(e)
    
    def list_projects(self):
        files = list(self.config_manager.projects_dir.glob("*.json"))
        if not files:
            print("No projects found")
            return
        
        print("\n=== FunkNotes Projects ===")
        for file in files:
            with open(file, 'r') as f:
                data = json.load(f)
                primary = " (PRIMARY)" if data["index"] == self.config.get("primary_project") else ""
                print(f"  [{data['index']}] {data['name']}{primary}")
    
    def show(self, object_name=None):
        project = self.get_primary_project()
        if project:
            if object_name:
                project.show_items(object_name)
            else:
                project.show_objects()
    
    def history(self, object_name):
        project = self.get_primary_project()
        if project:
            project.show_history(object_name)


def main():
    parser = argparse.ArgumentParser(description="FunkNotes - Git-like note taking")
    subparsers = parser.add_subparsers(dest="command", help="Commands")
    
    # New project
    new_parser = subparsers.add_parser("new", help="Create a new project")
    new_parser.add_argument("name", help="Project name")
    
    # Set primary
    primary_parser = subparsers.add_parser("primary", help="Set primary project")
    primary_parser.add_argument("project", help="Project name or index")
    
    # Add object
    obj_parser = subparsers.add_parser("object", help="Create a new object")
    obj_parser.add_argument("name", help="Object name (e.g., TODO)")
    
    # Add item
    add_parser = subparsers.add_parser("add", help="Add item to an object")
    add_parser.add_argument("object", help="Object name")
    add_parser.add_argument("text", nargs="+", help="Item text")
    
    # List projects
    subparsers.add_parser("list", help="List all projects")
    
    # Show items
    show_parser = subparsers.add_parser("show", help="Show objects or items")
    show_parser.add_argument("object", nargs="?", help="Object name (optional)")
    
    # Show history
    history_parser = subparsers.add_parser("history", help="Show object history")
    history_parser.add_argument("object", help="Object name")
    
    args = parser.parse_args()
    
    if not args.command:
        parser.print_help()
        return
    
    fn = FunkNotes()
    
    if args.command == "new":
        fn.new_project(args.name)
    elif args.command == "primary":
        fn.set_primary(args.project)
    elif args.command == "object":
        fn.add_object(args.name)
    elif args.command == "add":
        fn.add_item(args.object, " ".join(args.text))
    elif args.command == "list":
        fn.list_projects()
    elif args.command == "show":
        fn.show(args.object)
    elif args.command == "history":
        fn.history(args.object)


if __name__ == "__main__":
    main()
